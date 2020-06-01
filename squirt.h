#pragma once

#ifndef _WIN32
#include <netinet/in.h>
#define _O_BINARY 0
#else
#include <stdint.h>
#include <winsock.h>
#endif

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

typedef struct dir_entry_list {
  dir_entry_t* head;
  dir_entry_t* tail;
  struct dir_entry_list *next;
  struct dir_entry_list *prev;
} dir_entry_list_t;

extern const char* squirt_argv0;
extern int squirt_screenWidth;

const char*
util_formatNumber(int number);

int
util_connect(const char* hostname, uint32_t commandCode);

const char*
util_getHomeDir(void);

const char*
util_getHistoryFile(void);

int
util_mkdir(const char *path, uint32_t mode);

int
util_open(const char* filename, uint32_t mode);

char*
util_recvLatin1AsUtf8(int socketFd, uint32_t length);

int
util_sendLengthAndUtf8StringAsLatin1(int socketFd, const char* str);

const char*
util_getErrorString(uint32_t error);

size_t
util_recv(int socket, void *buffer, size_t length, int flags);

int
util_recvU32(int socketFd, uint32_t *data);

int
util_recv32(int socketFd, int32_t *data);

int
util_sendU32(int socketFd, uint32_t data);

const char*
util_amigaBaseName(const char* filename);

void
util_printFormatSpeed(int32_t size, double elapsed);

void
util_printProgress(struct timeval* start, uint32_t total, uint32_t fileLength);

int
squirt_exec(int argc, char* argv[]);

int
squirt_execCmd(const char* hostname, int argc, char** argv);

int
squirt_cli(int argc, char* argv[]);

int
squirt_dir(int argc, char* argv[]);

dir_entry_list_t*
squirt_dirRead(const char* hostname, const char* command);

void
squirt_dirFreeEntryLists(void);

void
squirt_dirFreeEntryList(dir_entry_list_t* list);

void
squirt_dirPrintEntryList(const char* hostname, dir_entry_list_t* list);

int
squirt_cwd(int argc, char* argv[]);

const char*
squirt_cwdRead(const char* hostname);

int
squirt_backup(int argc, char* argv[]);

int
squirt_suck(int argc, char* argv[]);

int
squirt(int argc, char* argv[]);

int
squirt_file(const char* hostname, const char* filename, const char* destFilename, int writeToCurrentDir, int progress);

int32_t
squirt_suckFile(const char* hostname, const char* filename, int progress, const char* destFilename);

void
util_onCtrlC(void (*handler)(void));

size_t
util_strlcat(char * restrict dst, const char * restrict src, size_t maxlen);

const char*
util_getTempFolder(void);
