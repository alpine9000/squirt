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

const char* main_argv0;
int main_screenWidth = 0;


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
  fprintf(stderr, "%s: ", main_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  main_cleanupAndExit(EXIT_FAILURE);
}


int main(int argc, char* argv[])
{
  main_argv0 = argv[0];

  setlocale(LC_NUMERIC, "");

  main_screenWidth = 80;
#ifndef _WIN32
  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != -1) {
    main_screenWidth = ws.ws_col;
  }
#else
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

  if (strstr(basename(argv[0]), "squirt_suck")) {
    suck_main(argc, argv);
  } else  if (strstr(basename(argv[0]), "squirt_exec")) {
    exec_main(argc, argv);
  } else if (strstr(basename(argv[0]), "squirt_cli")) {
    cli_main(argc, argv);
  } else if (strstr(basename(argv[0]), "squirt_dir")) {
    dir_main(argc, argv);
  } else if (strstr(basename(argv[0]), "squirt_backup")) {
    backup_main(argc, argv);
  } else if (strstr(basename(argv[0]), "squirt_cwd")) {
    cwd_main(argc, argv);
  } else {
    squirt_main(argc, argv);
  }

  main_cleanupAndExit(EXIT_SUCCESS);
}
