#include <string.h>
#include <libgen.h>
#include "squirt.h"

int main(int argc, char* argv[])
{
  if (strcmp(basename(argv[0]), "squirt_suck") == 0) {
    return squirt_suck(argc, argv);
  }

  if (strcmp(basename(argv[0]), "squirt_exec") == 0) {
    return squirt_cli(argc, argv);
  }

  return squirt(argc, argv);
}
