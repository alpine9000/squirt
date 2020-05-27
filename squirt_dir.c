#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
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
  vfprintf(stderr, format, args);
  va_end(args);
  cleanupAndExit(EXIT_FAILURE);
}


typedef struct direntry {
  const char* name;
  int32_t type;
  uint32_t size;
  struct direntry* next;
  int renderedSizeLength;
} dir_entry_t;

typedef struct {
  dir_entry_t* head;
  dir_entry_t* tail;
} dir_entry_list_t;

static void
pushDirEntry(dir_entry_list_t* list, const char* name, int32_t type, uint32_t size)
{
  dir_entry_t* entry = malloc(sizeof(dir_entry_t));
  if (list->tail == 0) {
    list->head = list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }


  entry->next = 0;
  entry->name = name;
  entry->type = type;
  entry->size = size;
}

static void
freeEntryList(dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;

  while (entry) {
    dir_entry_t* ptr = entry;
    entry = entry->next;
    free((void*)ptr->name);
    free(ptr);
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
    for (int i = 0; i < maxSizeLength-entry->renderedSizeLength + 3; i++) {
      putchar(' ');
    }

    if (printf("%'d %s%c\n", entry->size,  entry->name, entry->type > 0 ? '/' : ' ') < 0) {
      perror("printf");
    }
    //    for (unsigned int i = 0; i < strlen(entry->name); i++) {
    //      printf("%03d ", (unsigned char)entry->name[i]);
    //    }
    //printf("\n");
    entry = entry->next;
  }
}

static uint32_t
getDirEntry(dir_entry_list_t* entryList)
{
  uint32_t nameLength;

  if (recv(socketFd, &nameLength, sizeof(nameLength), 0) != sizeof(nameLength)) {
    fatalError("%s: failed to read name length\n", squirt_argv0);
  }

  nameLength = ntohl(nameLength);

  if (nameLength == 0) {
    return 0;
  }

  int32_t type;

  if (recv(socketFd, &type, sizeof(type), 0) != sizeof(type)) {
    fatalError("%s: failed to read type\n", squirt_argv0);
  }

  type = ntohl(type);

  char* buffer = malloc(nameLength+1);

  if (recv(socketFd, buffer, nameLength, 0) != nameLength) {
    fatalError("%s: failed to read name (%d bytes)\n", squirt_argv0, nameLength);
  }

  buffer[nameLength] = 0;

  uint32_t fileSize;

  if (recv(socketFd, &fileSize, sizeof(fileSize), 0) != sizeof(fileSize)) {
    fatalError("%s: failed to read file size\n", squirt_argv0);
  }

  fileSize = ntohl(fileSize);

  //  printf("%s%c\t\t%d\n", buffer, type > 0 ? '/' : ' ', fileSize);

  pushDirEntry(entryList, buffer, type, fileSize);

  return 1;
}

static void
squirt_processDir(const char* command, void(*process)(dir_entry_list_t*))
{
  struct sockaddr_in sockAddr;
  uint8_t commandCode = SQUIRT_COMMAND_DIR;
  int commandLength = 0;

  setlocale(LC_NUMERIC, "");
  setlocale(LC_ALL, "");

  commandLength = strlen(command);

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }

  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed\n");
  }

  int32_t networkCommandLength = htonl(commandLength);

  if (send(socketFd, &networkCommandLength, sizeof(networkCommandLength), 0) != sizeof(commandLength)) {
    fatalError("send() nameLength failed\n");
  }

  if (send(socketFd, command, commandLength, 0) != commandLength) {
    fatalError("send() command failed\n");
  }

  uint32_t more;
  dir_entry_list_t entryList = {0};
  do {
    more = getDirEntry(&entryList);
  } while (more);

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("%s: failed to read remote status\n", squirt_argv0);
  }

  if (process) {
    process(&entryList);
  }

  freeEntryList(&entryList);


  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s: %s\n", squirt_argv0, util_getErrorString(error));
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
  uint32_t commandLength = strlen(dir);

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }


  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed\n");
  }

  int32_t networkCommandLength = htonl(commandLength);

  if (send(socketFd, &networkCommandLength, sizeof(networkCommandLength), 0) != sizeof(commandLength)) {
    fatalError("send() nameLength failed\n");
  }

  if (send(socketFd, dir, commandLength, 0) != commandLength) {
    fatalError("send() command failed\n");
  }

  uint8_t c;
  while (recv(socketFd, &c, 1, 0)) {
    if (c == 0) {
      break;
    } else if (c == 0x9B) {
      fprintf(stdout, "%c[", 27);
      fflush(stdout);
    } else {
      write(1, &c, 1);
    }
  }

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("failed to read remote status\n");
  }

  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s: %s\n", squirt_argv0, util_getErrorString(error));
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


static void
pushDir(const char* dir)
{
  if (currentDir) {
    // char* newDir = malloc(strlen(currentDir) + 1 + strlen(dir) + 1);
    //sprintf(newDir, "%s/%s", currentDir, dir);
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
}

static void
popDir(void)
{
  for (int i = strlen(currentDir)-1; i >= 0; --i) {
    if (currentDir[i] == '/' || (i > 0 && currentDir[i-1] == ':')) {
      currentDir[i] = 0;
      break;
    }
  }
}


void
backupList(dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;

  while (entry) {
    if (entry->type < 0) {
      const char* path = fullPath(entry->name);
      char* utf8 = util_latin1ToUtf8(entry->name);
      struct stat st;
      int s = stat(utf8, &st);
      free(utf8);
      if (s != -1 && st.st_size == entry->size) {
	fflush(stdout);
	//printf("skipping file %s (local copy exists)\n", path);
	printf(".");
	fflush(stdout);
      } else {
	squirt_suckFile(hostname, path);
	fflush(stdout);
      }
      free((void*)path);
    }
    entry = entry->next;
  }

  entry = list->head;
  while (entry) {
    if (entry->type > 0) {
      squirt_backupDir(entry->name);
    }
    entry = entry->next;
  }
}

static char*
safeName(const char* name)
{
  char* safe = malloc(strlen(name)+1);
  strcpy(safe, name);
  for (unsigned int i = 0; i < strlen(safe); i++) {
    if (safe[i] == ':') {
      safe[i] = '_';
    }
  }
  char* utf8 = util_latin1ToUtf8(safe);
  free(safe);
  return utf8;
}

static void
squirt_backupDir(const char* dir)
{
  pushDir(dir);

  char* utf8 = util_latin1ToUtf8(currentDir);
  printf("\nbacking up dir %s", utf8);
  free(utf8);

  char* safe = safeName(dir);
  int mkdirResult = mkdir(safe, 0777);

  if (mkdirResult != 0 && errno != EEXIST) {
    fatalError("%s: failed to mkdir %s\n", squirt_argv0, safe);
  }

  const char* cwd = getcwd(0, 0);

  if (chdir(safe) == -1) {
    fatalError("%s: unable to chdir to %s\n", squirt_argv0, safe);
  }

  free(safe);

  squirt_processDir(currentDir, backupList);

  popDir();

  chdir(cwd);
  free((void*)cwd);

}


int
squirt_dir(int argc, char* argv[])
{
  squirt_argv0 = argv[0];

  if (argc != 3) {
    fatalError("usage: %s hostname dir_name\n", squirt_argv0);
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
    fatalError("usage: %s hostname dir_name\n", squirt_argv0);
  }

  hostname = argv[1];

  squirt_backupDir(argv[2]);

  exit(EXIT_SUCCESS);

  return 0;
}
