#pragma once

#include <netinet/in.h>

int
util_getSockAddr(const char * host, int port, struct sockaddr_in * addr);

const char*
util_getErrorString(uint32_t error);

int
squirt_cli(int argc, char* argv[]);

int
squirt_suck(int argc, char* argv[]);

int
squirt(int argc, char* argv[]);
