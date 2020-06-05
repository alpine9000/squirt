#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <sys/stat.h>

#include "main.h"
#include "common.h"


static void
backup_backupDir(const char* hostname, const char* dir);

static const char* SQUIRT_EXALL_INFO_DIR_NAME = ".__squirt/";
static char* backup_currentDir = 0;
static char* backup_skipFile = 0;
static int backup_socketFd = 0;
static char* backup_dirBuffer = 0;


void
backup_cleanup()
{
  if (backup_currentDir) {
    free(backup_currentDir);
    backup_currentDir = 0;
  }

  if (backup_skipFile) {
    free(backup_skipFile);
    backup_skipFile = 0;
  }

  if (backup_socketFd) {
    close(backup_socketFd);
    backup_socketFd = 0;
  }

  if (backup_dirBuffer) {
    free(backup_dirBuffer);
    backup_dirBuffer = 0;
  }
}


static char*
_scanString(FILE* fp)
{
  char buffer[108] = {0};
  int c = fscanf(fp, "%*[^:]:%107[^\n]%*c", buffer);
  if (c == 1) {
    char* str = malloc(strlen(buffer)+1);
    strcpy(str, buffer);
    return str;
  } else {
    return 0;
  }
}


static int
_scanInt(FILE* fp)
{
  int number = 0;
  if (fscanf(fp, "%*[^:]:%d%*c", &number) == 1) {
    return number;
  } else {
    return -1;
  }
}


static char*
_scanComment(FILE* fp)
{
  char buffer[128] = {0};
  char* ptr = buffer;
  int len;
  char c;

  do {
    len = fread(&c, 1, 1, fp);
    ptr++;
  } while (len > 0 && c != ':');

  size_t i = 0;
  do {
    len = fread(&buffer[i], 1, 1, fp);
    i++;
  } while (len > 0 && i < sizeof(buffer));

  char* comment = 0;
  if (strlen(buffer) > 0) {
    comment = malloc(strlen(buffer)+1);
    strcpy((char*)comment, buffer);
  }

  return comment;
}



static int
backup_readExAllData(dir_entry_t* entry, const char* path)
{
  if (!entry) {
    fatalError("readExAllData called with null entry");
  }
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  util_mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "r+");
  if (!fp) {
    free(name);
    return 0;
  }
  entry->name = _scanString(fp);
  entry->type = _scanInt(fp);
  entry->size = _scanInt(fp);
  entry->prot = _scanInt(fp);
  entry->days = _scanInt(fp);
  entry->mins = _scanInt(fp);
  entry->ticks = _scanInt(fp);
  entry->comment = _scanComment(fp);

  fclose(fp);
  free(name);

  return 1;
}

static char*
backup_fullPath(const char* name)
{
  if (!backup_currentDir) {
    return strdup(name);
  }

  char* path = malloc(strlen(backup_currentDir) + strlen(name) + 2);
  if (!path) {
    return NULL;
  }
  if (backup_currentDir[strlen(backup_currentDir)-1] != ':') {
    sprintf(path, "%s/%s", backup_currentDir, name);
  } else {
    sprintf(path, "%s%s", backup_currentDir, name);
  }
  return path;
}


static int
backup_saveExAllData(dir_entry_t* entry, const char* path)
{
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  util_mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "w");

  if (!fp) {
    free(name);
    return 0;
  }


  struct timeval tv ;
  int sec = entry->ticks / 50;
  tv.tv_sec = (DIR_AMIGA_EPOC_ADJUSTMENT_DAYS*24*60*60)+(entry->days*(24*60*60)) + (entry->mins*60) + sec;
  tv.tv_usec = (entry->ticks - (sec * 50)) * 200;
  time_t _time = tv.tv_sec;

  struct tm *tm = gmtime(&_time);
  struct utimbuf ut;

  struct stat st;

  if (stat(baseName, &st) != 0) {
    fatalError("failed to get file attributes of %s %s\n", baseName, backup_fullPath(entry->name));
  }

  ut.actime = st.st_atime;
  ut.modtime = mktime(tm);

  if (utime(baseName, &ut) != 0) {
    fatalError("failed to set file attributes of %s\n", baseName);
  }

  fprintf(fp, "name:%s\n", entry->name);
  fprintf(fp, "type:%d\n", entry->type);
  fprintf(fp, "size:%d\n", entry->size);
  fprintf(fp, "prot:%d\n", entry->prot);
  fprintf(fp, "days:%d\n", entry->days);
  fprintf(fp, "mins:%d\n", entry->mins);
  fprintf(fp, "ticks:%d\n", entry->ticks);
  if (entry->comment) {
    fprintf(fp, "comment:%s", entry->comment);
  } else {
    fprintf(fp, "comment:");
  }
  fclose(fp);

  free(name);
  return 1;
}


static int
backup_identicalExAllData(dir_entry_t* one, dir_entry_t* two)
{
  int identical =
    one->name != NULL && two->name != NULL &&
    strcmp(one->name, two->name) == 0 &&
    ((one->comment == 0 && two->comment == 0)||
     (one->comment != 0 && two->comment != 0 && strcmp(one->comment, two->comment) == 0)) &&
    one->type == two->type &&
    one->size == two->size &&
    one->prot == two->prot &&
    one->days == two->days &&
    one->mins == two->mins &&
    one->ticks == two->ticks;

#if 0
  if (!identical) {
    printf(">%s<>%s< %d\n", one->name, two->name, strcmp(one->name, two->name) );
    printf("%d %d\n", one->comment == 0, two->comment == 0);
    if (one->comment != 0) {
      printf("1:>%s<\n", one->comment);
    }
    if (two->comment != 0) {
      printf("2:>%s<\n", two->comment);
    }
    printf("%d %d %d\n", one->type, two->type, one->type == two->type);
    printf("%d %d %d\n", one->size, two->size, one->size == two->size);
    printf("%d %d %d\n", one->prot, two->prot, one->prot == two->prot);
    printf("%d %d %d\n", one->days, two->days, one->days == two->days);
    printf("%d %d %d\n", one->mins, two->mins, one->mins == two->mins);
    printf("%d %d %d\n", one->ticks, two->ticks, one->ticks == two->ticks);
  }
#endif

  return identical;
}




static void
backup_backupList(const char* hostname, dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;

  while (entry) {
    if (entry->type < 0) {
      const char* path = backup_fullPath(entry->name);
      int skipFile = 0;
      if (backup_skipFile) {
	char* found = strstr(backup_skipFile, path);
	if (found) {
	  found += strlen(path);
	  skipFile = *found == 0 || *found == '\n' || *found == '\r';
	}
      }
      int skip = skipFile;
      {
	dir_entry_t *temp = dir_newDirEntry();
	struct stat st;
	if (stat(util_amigaBaseName(path), &st) == 0) {
	  if (st.st_size == (off_t)entry->size) {
	    skip = backup_readExAllData(temp, path);
	    if (skip) {
	      skip = backup_identicalExAllData(temp, entry);
	    }
	  }
	}
	dir_freeEntry(temp);
      }

      if (skip) {
	if (skipFile) {
	  printf("%c[1m%s ***SKIPPED***%c[0m\n", 27, path, 27);
	} else {
	  printf("%s \xE2\x9C\x93\n", path);
	}
      } else {
	uint32_t protect;
	if (squirt_suckFile(hostname, path, 1, 0, &protect) < 0) {
	  fatalError("failed to backup %s", path);
	}
	backup_saveExAllData(entry, path);
	printf("\n");
	fflush(stdout);
      }
      free((void*)path);
    }
    entry = entry->next;
  }

  entry = list->head;
  while (entry) {
    if (entry->type > 0) {
      const char* path = backup_fullPath(entry->name);
      int skipFile = 0;
      if (backup_skipFile) {
	char* found = strstr(backup_skipFile, path);
	if (found) {
	  found += strlen(path);
	  skipFile = *found == 0 || *found == '\n' || *found == '\r';
	}
      }
      if (!skipFile) {
	backup_backupDir(hostname, entry->name);
	backup_saveExAllData(entry, path);
	free((void*)path);
      } else {
	printf("%c[1m%s ***SKIPPED***%c[0m\n", 27, path, 27);
	free((void*)path);
      }

    }
    entry = entry->next;
  }
}


static int
backup_cd(const char* hostname, const char* dir)
{
  uint32_t error = 0;

  if ((backup_socketFd = util_connect(hostname, SQUIRT_COMMAND_CD)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(backup_socketFd, dir) != 0) {
    fatalError("send() command failed");
  }

  if (util_recvU32(backup_socketFd, &error) != 0) {
    fatalError("cd: failed to read remote status");
  }

  return error;
}



static char*
backup_safeName(const char* name)
{
  char* safe = malloc(strlen(name)+1);
  char* dest = safe;
  if (dest) {
    while (*name) {
      if (*name != ':') {
	*dest = *name;
	dest++;
      }
      name++;
    }
    *dest = 0;
  }

  return safe;
}



static char*
backup_pushDir(const char* hostname, const char* dir)
{
  if (backup_currentDir) {
    char* newDir = backup_fullPath(dir);
    if (backup_currentDir) {
      free(backup_currentDir);
    }
    backup_currentDir = newDir;
  } else {
    backup_currentDir = malloc(strlen(dir)+1);
    strcpy(backup_currentDir, dir);
  }

  if (backup_cd(hostname, backup_currentDir) != 0) {
    fatalError("unable to backup %s", backup_currentDir);
  }

  char* safe = backup_safeName(dir);
  if (!safe) {
    fatalError("failed to create safe name");
  }

  int mkdirResult = util_mkdir(safe, 0777);

  if (mkdirResult != 0 && errno != EEXIST) {
    fatalError("failed to mkdir %s", safe);
  }

  char* cwd = getcwd(0, 0);

  if (chdir(safe) == -1) {
    fatalError("unable to chdir to %s", safe);
  }

  free(safe);

  return cwd;
}


static void
backup_popDir(char* cwd)
{
  for (int i = strlen(backup_currentDir)-1; i >= 0; --i) {
    if (backup_currentDir[i] == '/' || (i > 0 && backup_currentDir[i-1] == ':')) {
      backup_currentDir[i] = 0;
      break;
    }
  }

  if (chdir(cwd)) {
    fatalError("failed to cd to %s", cwd);
  }
  free((void*)cwd);
}


static void
backup_backupDir(const char* hostname, const char* dir)
{
  char* cwd = backup_pushDir(hostname, dir);
  printf("%s/ \xE2\x9C\x93\n", backup_currentDir);
  if (dir_process(hostname, backup_currentDir, backup_backupList) != 0) {
    fatalError("unable to read %s", dir);
  }

  backup_popDir(cwd);
}


static void
backup_loadSkipFile(const char* filename)
{
  struct stat st;

  if (stat(filename, &st) == -1) {
    fatalError("filed to stat %s", filename);
  }

  int fileLength = st.st_size;
  backup_skipFile = malloc(fileLength+1);
  memset(backup_skipFile, 0, fileLength+1);
  int fd = open(filename,  O_RDONLY|_O_BINARY);
  if (fd) {
    if (read(fd, backup_skipFile, fileLength) != fileLength) {
      close(fd);
      fatalError("failed to read skipfile %s", filename);
    }
  } else {
    fatalError("failed to open skipfile %s", filename);
  }

  close(fd);
}



void
backup_main(int argc, char* argv[])
{
  backup_skipFile = 0;
  backup_currentDir = 0;
  const char* hostname;
  char* path;

  if (argc < 3) {
    fatalError("incorrect number of arguments\nusage: %s [--skipfile=skipfile] hostname dir_name", main_argv0);
  }

  if (argc == 4) {
    if (strstr(argv[1], "--skipfile=") == 0) {
      fatalError("incorrect number of arguments\nusage: %s [--skipfile=skipfile] hostname dir_name", main_argv0);
    }
    hostname = argv[2];
    path = argv[3];
    backup_loadSkipFile(strstr(argv[1], "=")+1);
  } else {
    hostname = argv[1];
    path = argv[2];
  }


  char* token = strtok(path, ":");
  char* dir = 0;
  if (token) {
    dir = token;
    token = strtok(0, "/");
    if (token) {
      backup_dirBuffer = malloc(strlen(dir)+2);
      if (!backup_dirBuffer) {
	fatalError("malloc failed");
      }
      sprintf(backup_dirBuffer, "%s:", dir);
      free(backup_pushDir(hostname, backup_dirBuffer));
      do {
	dir = token;
	token = strtok(0, "/");
	if (token) {
	  free(backup_pushDir(hostname, dir));
	}
      } while (token);
    } else {
      backup_dirBuffer = malloc(strlen(dir)+2);
      if (!backup_dirBuffer) {
	fatalError("malloc failed");
      }
      sprintf(backup_dirBuffer, "%s:", dir);
      dir = backup_dirBuffer;
    }
  }

  if (dir) {
    backup_backupDir(hostname, dir);
  }


  printf("\nbackup complete!\n");
}
