#pragma once
#include <stdint.h>
//#define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
#include <stdio.h>
#define fatalError(x) _fatalError(x)
#else
#define printf(...)
#define fprintf(...)
#define fatalError(x) _fatalError()
#endif

extern uint32_t squirtd_error;
void exec_run(const char* command, int socketFd);
void exec_cd(const char* dir, int socketFd);
void exec_dir(const char* dir, int socketFd);
uint32_t exec_cwd(int socketFd);
