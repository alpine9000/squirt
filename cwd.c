#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "argv.h"
#include "main.h"
#include "common.h"

void
cwd_cleanup(void)
{

}


const char*
cwd_read(void)
{
  static const char* command = "cwd";

  if (util_sendCommand(main_socketFd, SQUIRT_COMMAND_CWD) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, command) != 0) {
    fatalError("send() command failed");
  }

  uint32_t nameLength;
  if (util_recvU32(main_socketFd, &nameLength) != 0) {
    fatalError("failed to get cwd length");
  }

  char *cwd = util_recvLatin1AsUtf8(main_socketFd, nameLength);

  if (!cwd) {
    fatalError("failed to get cwd");
  }

  uint32_t error;
  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("cwd: failed to read remote status");
  }

  cwd_cleanup();

  return cwd;
}


void
cwd_main(int argc, char* argv[])
{
  if (argc < 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", argv[0]);
  }


  util_connect(argv[1]);
  const char* cwd = cwd_read();

  if (!cwd) {
    fatalError("cwd failed");
  }

  printf("%s\n", cwd);

  free((void*)cwd);

}
