#include <string.h>
#include <libgen.h>
#include <locale.h>
#include <sys/time.h>
#include "squirt.h"
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>

const char* squirt_argv0;
int squirt_screenWidth = 0;

int main(int argc, char* argv[])
{
  squirt_argv0 = argv[0];

  setlocale(LC_NUMERIC, "");

  struct winsize ws;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
    squirt_screenWidth = 80;
  } else {
    squirt_screenWidth = ws.ws_col;
  }

  if (strcmp(basename(argv[0]), "squirt_suck") == 0) {
    return squirt_suck(argc, argv);
  }

  if (strcmp(basename(argv[0]), "squirt_exec") == 0) {
    return squirt_cli(argc, argv);
  }

  if (strcmp(basename(argv[0]), "squirt_dir") == 0) {
    return squirt_dir(argc, argv);
  }

  if (strcmp(basename(argv[0]), "squirt_backup") == 0) {
    return squirt_backup(argc, argv);
  }

  return squirt(argc, argv);
}
