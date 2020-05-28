#pragma once

#include <netinet/in.h>

extern const char* squirt_argv0;

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
