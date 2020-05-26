#pragma once

#ifdef AMIGA
typedef BPTR file_handle_t;
#define SOCKLEN_T LONG
//#include <stdio.h>
#define printf(...)
#define fprintf(...)
#define fatalError(x) _fatalError()
#else
typedef int file_handle_t;
typedef int32_t LONG;
#define fatalError(x) _fatalError(x)
#define DeleteFile(x)
#define Open(x, y) open(x, y, 0777)
#define Close(x) close(x)
#define CloseSocket(x) close(x)
#define Write(x, y, z) write(x, y, z)
#define SOCKLEN_T socklen_t
#define MODE_READWRITE (O_WRONLY|O_CREAT)
#define MODE_NEWFILE (O_WRONLY|O_CREAT)
#endif

uint32_t exec_run(const char* command, int socketFd);
uint32_t exec_cd(const char* dir, int socketFd);
