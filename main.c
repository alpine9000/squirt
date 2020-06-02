#include <string.h>
#include <libgen.h>
#include <locale.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>

#include "main.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#endif

const char* squirt_argv0;
int squirt_screenWidth = 0;

_Noreturn void
main_cleanupAndExit(int errorCode)
{
  backup_cleanup();
  cli_cleanup();
  cwd_cleanup();
  exec_cleanup();
  suck_cleanup();
  dir_cleanup();
  srl_cleanup();
  squirt_cleanup();
  exit(errorCode);
}


void
main_fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s: ", squirt_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  main_cleanupAndExit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
  squirt_argv0 = argv[0];

  setlocale(LC_NUMERIC, "");

  squirt_screenWidth = 80;
#ifndef _WIN32
  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != -1) {
    squirt_screenWidth = ws.ws_col;
  }
#else
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

  if (strstr(basename(argv[0]), "squirt_suck")) {
    return suck_main(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_exec")) {
    return exec_main(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_cli")) {
    return cli_main(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_dir")) {
    return dir_main(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_backup")) {
    return backup_main(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_cwd")) {
    return cwd_main(argc, argv);
  }

  return squirt_main(argc, argv);
}
