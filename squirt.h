#pragma once

#ifndef _WIN32
#include <netinet/in.h>
#else
#include <stdint.h>
#include <winsock.h>
#endif

extern const char* squirt_argv0;
extern int squirt_screenWidth;

const char*
util_formatNumber(int number);

int
util_mkdir(const char *path, uint32_t mode);

int
util_open(const char* filename, uint32_t mode);

char*
util_recvLatin1AsUtf8(int socketFd, uint32_t length);

int
util_sendLengthAndUtf8StringAsLatin1(int socketFd, const char* str);

int
util_getSockAddr(const char * host, int port, struct sockaddr_in * addr);

const char*
util_getErrorString(uint32_t error);

size_t
util_recv(int socket, void *buffer, size_t length, int flags);

int
util_recvU32(int socketFd, uint32_t *data);

const char*
util_amigaBaseName(const char* filename);

void
util_printFormatSpeed(int32_t size, double elapsed);

void
util_printProgress(struct timeval* start, uint32_t total, uint32_t fileLength);

int
squirt_cli(int argc, char* argv[]);

int
squirt_dir(int argc, char* argv[]);

int
squirt_backup(int argc, char* argv[]);

int
squirt_suck(int argc, char* argv[]);

int
squirt(int argc, char* argv[]);

uint32_t
squirt_suckFile(const char* hostname, const char* filename);
