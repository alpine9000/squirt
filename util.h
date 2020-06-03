#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

const char*
util_formatNumber(int number);

int
util_connect(const char* hostname, uint32_t commandCode);

const char*
util_getHomeDir(void);

const char*
util_getHistoryFile(void);

int
util_mkpath(const char *dir);

int
util_rmdir(const char *dir);

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

void
util_onCtrlC(void (*handler)(void));

size_t
util_strlcat(char * restrict dst, const char * restrict src, size_t maxlen);

const char*
util_getTempFolder(void);

int
util_isDirectory(const char *path);

int
util_system(char** argv);
