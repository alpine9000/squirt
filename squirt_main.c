#include <string.h>
#include <libgen.h>
#include "squirt.h"

const char* squirt_argv0;

int main(int argc, char* argv[])
{
  squirt_argv0 = argv[0];

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
