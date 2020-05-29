#include <string.h>
#include <libgen.h>
#include <locale.h>
#include <sys/time.h>
#include "squirt.h"
#include <stdio.h>
#include <unistd.h>

#ifndef _WIN32
#include <sys/ioctl.h>
#endif

const char* squirt_argv0;
int squirt_screenWidth = 0;

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
    return squirt_suck(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_exec")) {
    return squirt_cli(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_dir")) {
    return squirt_dir(argc, argv);
  }

  if (strstr(basename(argv[0]), "squirt_backup")) {
    return squirt_backup(argc, argv);
  }

  return squirt(argc, argv);
}
