#pragma once

#define SOCKLEN_T LONG
//#include <stdio.h>
#define printf(...)
#define fprintf(...)
#define fatalError(x) _fatalError()

extern uint32_t squirtd_error;
void exec_run(const char* command, int socketFd);
void exec_cd(const char* dir, int socketFd);
void exec_dir(const char* dir, int socketFd);
