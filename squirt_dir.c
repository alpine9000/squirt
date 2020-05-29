#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <ncurses.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <locale.h>
#include <libgen.h>
#include <unistd.h>
#include <iconv.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "squirt.h"
#include "common.h"

static const char* SQUIRT_EXALL_INFO_DIR_NAME = ".__squirt/";
static char* hostname;
static char* currentDir;
static int socketFd = 0;

static void
cleanup(void)
{
  if (socketFd) {
    close(socketFd);
    socketFd = 0;
  }
}

static void
cleanupAndExit(int errorCode)
{
  cleanup();
  exit(errorCode);
}


static void
fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s: ", squirt_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  cleanupAndExit(EXIT_FAILURE);
}


typedef struct direntry {
  const char* name;
  int32_t type;
  uint32_t size;
  uint32_t prot;
  uint32_t days;
  uint32_t mins;
  uint32_t ticks;
  const char* comment;
  struct direntry* next;
  int renderedSizeLength;
} dir_entry_t;

typedef struct {
  dir_entry_t* head;
  dir_entry_t* tail;
} dir_entry_list_t;

static dir_entry_t*
newDirEntry(void)
{
  return calloc(1, sizeof(dir_entry_t));
}

static void
pushDirEntry(dir_entry_list_t* list, const char* name, int32_t type, uint32_t size, uint32_t prot, uint32_t days, uint32_t mins, uint32_t ticks, const char* comment)
{
  dir_entry_t* entry = newDirEntry();

  if (list->tail == 0) {
    list->head = list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }

  entry->next = 0;
  entry->name = name;
  entry->type = type;
  entry->prot = prot;
  entry->days = days;
  entry->mins = mins;
  entry->ticks = ticks;
  entry->size = size;
  entry->comment = comment;
}

static void
freeEntry(dir_entry_t* ptr)
{
  if (ptr) {
    if (ptr->name) {
      free((void*)ptr->name);
    }
    if (ptr->comment) {
      free((void*)ptr->comment);
    }
    free(ptr);
  }
}

static void
freeEntryList(dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;

  while (entry) {
    dir_entry_t* ptr = entry;
    entry = entry->next;
    freeEntry(ptr);
  }
}


wchar_t *
ctow(const char *buf) {
  wchar_t *cr, *output;
  cr = output = malloc((strlen(buf)+1)*sizeof(wchar_t));
  while (*buf) {
    *output++ = *buf++;
  }
  *output = 0;
  return cr;
}

static void
printProtectFlags(dir_entry_t* entry)
{
  char bits[] = {'d', 'e', 'w', 'r', 'a', 'p', 's', 'h'};
  uint32_t prot = (entry->prot ^ 0xF) & 0xFF;
  for (int i = 7; i >= 0; i--) {
    if (prot & (1<<i)) {
      printf("%c", bits[i]);
    } else {
      printf("-");
    }
  }
}

static char*
formatDateTime(dir_entry_t* entry)
{
  struct timeval tv;
  time_t nowtime;
  struct tm *nowtm;
  static char tmbuf[64];

  int sec = entry->ticks / 50;
  tv.tv_sec = (8*365*24*60*60)+(entry->days*(24*60*60)) + (entry->mins*60) + sec;
  tv.tv_usec = (entry->ticks - (sec * 50)) * 200;
  nowtime = tv.tv_sec;
  nowtm = gmtime(&nowtime);
  strftime(tmbuf, sizeof tmbuf, "%m-%d-%y %H:%M:%S", nowtm);
  return tmbuf;
}

static void
printEntryList(dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;
  int maxSizeLength = 0;

  while (entry) {
    char buffer[255];
    snprintf(buffer, sizeof(buffer), "%'d", entry->size);
    entry->renderedSizeLength = strlen(buffer);
    if (entry->renderedSizeLength > maxSizeLength) {
      maxSizeLength = entry->renderedSizeLength;
    }
    entry = entry->next;
  }

  entry = list->head;
  while (entry) {
    printProtectFlags(entry);

    for (int i = 0; i < maxSizeLength-entry->renderedSizeLength + 3; i++) {
      putchar(' ');
    }

    if (printf("%'d %s %s%c", entry->size, formatDateTime(entry), entry->name, entry->type > 0 ? '/' : ' ') < 0) {
      perror("printf");
    }
    if (entry->comment) {
      printf(" (%s)", entry->comment);
    }
    printf("\n");
    entry = entry->next;
  }
}

static uint32_t
getDirEntry(dir_entry_list_t* entryList)
{
  uint32_t nameLength;

  if (!util_recvU32(socketFd, &nameLength)) {
    fatalError("failed to read name length");
  }

  if (nameLength == 0) {
    return 0;
  }

  char* buffer = util_recvLatin1AsUtf8(socketFd, nameLength);

  if (!buffer) {
    fatalError("failed to read name (%d bytes)", nameLength);
  }

  int32_t type;
  if (util_recv(socketFd, &type, sizeof(type), 0) != sizeof(type)) {
    fatalError("failed to read type");
  }
  type = ntohl(type);


  uint32_t size;
  if (!util_recvU32(socketFd, &size)) {
    fatalError("failed to read file size");
  }

  uint32_t prot;
  if (!util_recvU32(socketFd, &prot)) {
    fatalError("failed to read file prot");
  }

  uint32_t days;
  if (!util_recvU32(socketFd, &days)) {
    fatalError("failed to read file days");
  }


  uint32_t mins;
  if (!util_recvU32(socketFd, &mins)) {
    fatalError("failed to read file mins");
  }

  uint32_t ticks;
  if (!util_recvU32(socketFd, &ticks)) {
    fatalError("failed to read file ticks");
  }

  uint32_t commentLength;
  if (!util_recvU32(socketFd, &commentLength)) {
    fatalError("failed to read comment length");
  }

  char* comment;
  if (commentLength > 0) {
    comment = util_recvLatin1AsUtf8(socketFd, commentLength);
  } else {
    comment = 0;
  }

  pushDirEntry(entryList, buffer, type, size, prot, days, mins, ticks, comment);

  return 1;
}

static void
squirt_processDir(const char* command, void(*process)(dir_entry_list_t*))
{
  struct sockaddr_in sockAddr;
  uint8_t commandCode = SQUIRT_COMMAND_DIR;

  setlocale(LC_NUMERIC, "");

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed");
  }

  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed");
  }

  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, command)) {
    fatalError("send() command failed");
  }

  uint32_t more;
  dir_entry_list_t entryList = {0};
  do {
    more = getDirEntry(&entryList);
  } while (more);

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("failed to read remote status");
  }

  if (process) {
    process(&entryList);
  }

  freeEntryList(&entryList);


  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s", util_getErrorString(error));
  }
  cleanup();
}


static void
squirt_backupDir(const char* dir);

static void
squirt_cd(const char* dir)
{
  struct sockaddr_in sockAddr;
  uint8_t commandCode = SQUIRT_COMMAND_CD;

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed");
  }

  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed");
  }

  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, dir)) {
    fatalError("send() command failed");
  }

  uint8_t c;
  while (util_recv(socketFd, &c, 1, 0)) {
    if (c == 0) {
      break;
    } else if (c == 0x9B) {
      fprintf(stdout, "%c[", 27);
      fflush(stdout);
    } else {
      int ignore = write(1, &c, 1);
      (void)ignore;
    }
  }

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("failed to read remote status");
  }

  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s", util_getErrorString(error));
  }

}

static char*
fullPath(const char* name)
{
  char* path = malloc(strlen(currentDir) + strlen(name) + 2);
  if (currentDir[strlen(currentDir)-1] != ':') {
    sprintf(path, "%s/%s", currentDir, name);
  } else {
    sprintf(path, "%s%s", currentDir, name);
  }
  return path;
}

static char*
safeName(const char* name)
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
pushDir(const char* dir)
{
  if (currentDir) {
    char* newDir = fullPath(dir);
    if (currentDir) {
      free(currentDir);
    }
    currentDir = newDir;
  } else {
    currentDir = malloc(strlen(dir)+1);
    strcpy(currentDir, dir);
  }

  squirt_cd(currentDir);

  char* safe = safeName(dir);
  if (!safe) {
    fatalError("failed to create safe name");
  }
  int mkdirResult = mkdir(safe, 0777);

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
popDir(char* cwd)
{
  for (int i = strlen(currentDir)-1; i >= 0; --i) {
    if (currentDir[i] == '/' || (i > 0 && currentDir[i-1] == ':')) {
      currentDir[i] = 0;
      break;
    }
  }

  if (chdir(cwd)) {
    fatalError("failed to cd to %s", cwd);
  }
  free((void*)cwd);
}


static int
saveExAllData(dir_entry_t* entry, const char* path)
{
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "w");

  if (!fp) {
    free(name);
    return 0;
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


static char*
scanString(FILE* fp)
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
scanInt(FILE* fp)
{
  int number = 0;
  if (fscanf(fp, "%*[^:]:%d%*c", &number) == 1) {
    return number;
  } else {
    return -1;
  }
}

static char*
scanComment(FILE* fp)
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
readExAllData(dir_entry_t* entry, const char* path)
{
  if (!entry) {
    fatalError("readExAllData called with null entry");
  }
  const char* baseName = util_amigaBaseName(path);
  const char* ident = SQUIRT_EXALL_INFO_DIR_NAME;
  mkdir(ident, 0777);
  char* name = malloc(strlen(baseName)+1+strlen(ident));
  sprintf(name, "%s%s", ident, baseName);
  FILE *fp = fopen(name, "r+");
  if (!fp) {
    free(name);
    return 0;
  }
  entry->name = scanString(fp);
  entry->type = scanInt(fp);
  entry->size = scanInt(fp);
  entry->prot = scanInt(fp);
  entry->days = scanInt(fp);
  entry->mins = scanInt(fp);
  entry->ticks = scanInt(fp);
  entry->comment = scanComment(fp);

  fclose(fp);
  free(name);

  return 1;
}


static int
identicalExAllData(dir_entry_t* one, dir_entry_t* two)
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


void
backupList(dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;

  while (entry) {
    if (entry->type < 0) {
      const char* path = fullPath(entry->name);
      int skip = false;
      {
	dir_entry_t *temp = newDirEntry();
	struct stat st;
	if (stat(util_amigaBaseName(path), &st) == 0) {
	  if (st.st_size == entry->size) {
	    skip = readExAllData(temp, path);
	    if (skip) {
	      skip = identicalExAllData(temp, entry);
	    }
	  }
	}
	freeEntry(temp);
      }

      if (skip) {
	printf("%s \xE2\x9C\x93\n", path);
      } else {
	squirt_suckFile(hostname, path);
	saveExAllData(entry, path);
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
      const char* path = fullPath(entry->name);
      saveExAllData(entry, path);
      free((void*)path);
      squirt_backupDir(entry->name);
    }
    entry = entry->next;
  }
}


static void
squirt_backupDir(const char* dir)
{
  char* cwd = pushDir(dir);
  printf("%s/ \xE2\x9C\x93\n", currentDir);

  squirt_processDir(currentDir, backupList);

  popDir(cwd);
}


int
squirt_dir(int argc, char* argv[])
{
  squirt_argv0 = argv[0];

  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname dir_name", squirt_argv0);
  }

  hostname = argv[1];

  squirt_processDir(argv[2], printEntryList);

  exit(EXIT_SUCCESS);

  return 0;
}

int
squirt_backup(int argc, char* argv[])
{
  currentDir = 0;

  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname dir_name", squirt_argv0);
  }

  hostname = argv[1];

  char* dirBuffer = 0;
  char* path = argv[2];
  char* token = strtok(path, ":");
  char* dir = 0;
  if (token) {
    dir = token;
    token = strtok(0, "/");
    if (token) {
      dirBuffer = malloc(strlen(dir)+2);
      sprintf(dirBuffer, "%s:", dir);
      pushDir(dirBuffer);
      do {
	dir = token;
	token = strtok(0, "/");
	if (token) {
	  pushDir(dir);
	}
      } while (token);
    } else {
      dirBuffer = malloc(strlen(dir)+2);
      sprintf(dirBuffer, "%s:", dir);
      dir = dirBuffer;
    }
  }

  if (dir) {
    squirt_backupDir(dir);
  }

  if (dirBuffer) {
    free(dirBuffer);
  }

  if (currentDir) {
    free(currentDir);
  }

  printf("\nbackup complete!\n");

  exit(EXIT_SUCCESS);

  return 0;
}
