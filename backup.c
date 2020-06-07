#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <limits.h>
#include <getopt.h>
#include <sys/stat.h>

#include "main.h"
#include "common.h"
#include "exall.h"


static void
backup_backupDir(const char* dir);


static char* backup_currentDir = 0;
static char* backup_skipFile = 0;
static char* backup_dirBuffer = 0;
static int backup_prune = 0;

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

  if (backup_dirBuffer) {
    free(backup_dirBuffer);
    backup_dirBuffer = 0;
  }
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


static void
backup_pruneFiles(const char* filename, void* data)
{
  if (strcmp(filename, ".") == 0 ||
      strcmp(filename, "..") == 0 ||
      strcmp(filename, SQUIRT_EXALL_INFO_DIR) == 0) {
    return;
  }
  dir_entry_list_t* list = data;
  dir_entry_t* entry = list->head;
  int found = 0;
  while (entry) {
    if (strcmp(entry->name, filename) == 0) {
     found = 1;
      break;
    }
    entry = entry->next;
  }

  if (!found) {
    char exFilename[PATH_MAX];
    snprintf(exFilename, sizeof(exFilename), "%s%s", SQUIRT_EXALL_INFO_DIR_NAME, filename);
    char* path = backup_fullPath(filename);
    printf("%c[31m%s \xF0\x9F\x92\x80\xF0\x9F\x92\x80\xF0\x9F\x92\x80 REMOVED \xF0\x9F\x92\x80\xF0\x9F\x92\x80\xF0\x9F\x92\x80%c[0m\n", 27, path, 27); // red, utf-8 skulls
    free(path);
    if (unlink(filename) != 0 || unlink(exFilename) != 0) {
      fatalError("failed to remove %s\n", filename);
    }
  }
}

static void
backup_backupList(dir_entry_list_t* list)
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
	    skip = exall_readExAllData(temp, path);
	    if (skip) {
	      skip = exall_identicalExAllData(temp, entry);
	    }
	  }
	}
	dir_freeEntry(temp);
      }

      if (skip) {
	if (skipFile) {
	  printf("%c[1m%s ***SKIPPED***%c[0m\n", 27, path, 27); // bold
	} else {
	  printf("\xE2\x9C\x85 %s\n", path); // utf-8 tick
	}
      } else {
	uint32_t protect;
	if (squirt_suckFile(path, 1, 0, &protect) < 0) {
	  fatalError("failed to backup %s", path);
	}
	exall_saveExAllData(entry, path);
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
	backup_backupDir(entry->name);
	exall_saveExAllData(entry, path);
	free((void*)path);
      } else {
	printf("%c[1m%s ***SKIPPED***%c[0m\n", 27, path, 27);
	free((void*)path);
      }

    }
    entry = entry->next;
  }

  if (backup_prune) {
    util_dirOperation(".", backup_pruneFiles, list);
  }
}


static char*
backup_pushDir(const char* dir)
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

  if (util_cd(backup_currentDir) != 0) {
    fatalError("unable to backup %s", backup_currentDir);
  }

  char* safe = util_safeName(dir);
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
backup_backupDir(const char* dir)
{
  char* cwd = backup_pushDir(dir);
  printf("\xE2\x9C\x85 %s\n", backup_currentDir); // utf-8 tick
  if (dir_process(backup_currentDir, backup_backupList) != 0) {
    fatalError("unable to read %s", dir);
  }

  backup_popDir(cwd);
}


char*
backup_loadSkipFile(const char* filename)
{
  struct stat st;

  if (stat(filename, &st) == -1) {
    fatalError("filed to load skip file: %s", filename);
  }

  int fileLength = st.st_size;
  char* skipFile = malloc(fileLength+1);
  memset(skipFile, 0, fileLength+1);
  int fd = open(filename,  O_RDONLY|_O_BINARY);
  if (fd) {
    if (read(fd, skipFile, fileLength) != fileLength) {
      close(fd);
      fatalError("failed to read skipfile %s", filename);
    }
  } else {
    fatalError("failed to open skipfile %s", filename);
  }

  close(fd);
  return skipFile;
}


_Noreturn static void
backup_usage(void)
{
  fatalError("invalid arguments\nusage: %s [--prune] [--skipfile=skipfile] hostname dir_name", main_argv0);
}


void
backup_main(int argc, char* argv[])
{
  backup_skipFile = 0;
  backup_currentDir = 0;
  const char* hostname = 0;
  char* path = 0;
  char* skipfile = 0;


  while (optind < argc) {
    static struct option long_options[] =
      {
       {"prune",    no_argument, &backup_prune, 'p'},
       {"skipfile", required_argument, 0, 's'},
       {0, 0, 0, 0}
      };
    int option_index = 0;
    char c = getopt_long (argc, argv, "", long_options, &option_index);

    if (c != -1) {
      switch (c) {
      case 0:
	break;
      case 's':
	if (optarg == 0 || strlen(optarg) == 0) {
	  backup_usage();
	}
	skipfile = optarg;
	break;
      case '?':
      default:
	backup_usage();
	break;
      }
    } else {
      if (hostname == 0) {
	hostname = argv[optind];
      } else {
	path = argv[optind];
      }
      optind++;
    }
  }

  if (!hostname || !path) {
    backup_usage();
  }

  if (skipfile) {
    backup_skipFile = backup_loadSkipFile(skipfile);
  }

  util_connect(hostname);

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
      free(backup_pushDir(backup_dirBuffer));
      do {
	dir = token;
	token = strtok(0, "/");
	if (token) {
	  free(backup_pushDir(dir));
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
    backup_backupDir(dir);
  }


  printf("\nbackup complete!\n");
}
