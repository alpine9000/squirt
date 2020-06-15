#include "crc32.h"
//#include <stdio.h>
#include <proto/dos.h>

int
main(int argc, char** argv)
{
  if (argc != 2) {
    Printf((APTR)"usage: %s file\n", (int)argv[0]);
    return 1;
  }

  uint32_t crc;
  if (crc32_sum(argv[1], &crc) == 0) {
    Printf((APTR)"%lx\n", crc);
  } else {
    Printf((APTR)"error\n", 0);
    return 1;
  }
  return 0;
}
