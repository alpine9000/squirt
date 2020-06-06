#pragma once

#include "util.h"
#include "cli.h"
#include "cwd.h"
#include "exec.h"
#include "suck.h"
#include "srl.h"
#include "dir.h"
#include "main.h"
#include "backup.h"
#include "squirt.h"


#ifndef _WIN32
#include <netinet/in.h>
#define _O_BINARY 0
#else
#include <stdint.h>
#include <winsock.h>
#endif


extern const char* main_argv0;
extern int main_screenWidth;


_Noreturn void
main_fatalError(const char *format, ...);

_Noreturn void
main_cleanupAndExit(int errorCode);

#define fatalError(...) main_fatalError(__VA_ARGS__)
