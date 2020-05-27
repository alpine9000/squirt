#pragma once

#include <netinet/in.h>

extern const char* squirt_argv0;

char*
util_utf8ToLatin1(const char* buffer);

char*
util_latin1ToUtf8(const char* buffer);

int
util_getSockAddr(const char * host, int port, struct sockaddr_in * addr);

const char*
util_getErrorString(uint32_t error);

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

void
squirt_suckFile(const char* hostname, const char* filename);
