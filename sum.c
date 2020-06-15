#include "crc32.h"
#ifdef AMIGA
#include <proto/dos.h>
#else
#include <stdio.h>
#endif

int
main(int argc, char** argv)
{
  if (argc != 2) {
#ifdef AMIGA
    Printf((APTR)"usage: %s file\n", (int)argv[0]);
#else
    printf("usage: %s file\n", argv[0]);
#endif
    return 1;
  }

  uint32_t crc;
  if (crc32_sum(argv[1], &crc) == 0) {
#ifdef AMIGA
    Printf((APTR)"%lx\n", crc);
#else
    printf("%x\n", crc);
#endif
  } else {
#ifdef AMIGA
    Printf((APTR)"error\n", 0);
#else
    puts("error\n");
#endif
    return 1;
  }
  return 0;
}
