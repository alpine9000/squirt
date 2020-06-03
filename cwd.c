#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "argv.h"
#include "main.h"
#include "common.h"

static int cwd_socketFd = 0;

void
cwd_cleanup(void)
{
  if (cwd_socketFd > 0) {
    close(cwd_socketFd);
    cwd_socketFd = 0;
  }
}


const char*
cwd_read(const char* hostname)
{
  static const char* command = "cwd";
  cwd_socketFd = 0;

  if ((cwd_socketFd = util_connect(hostname, SQUIRT_COMMAND_CWD)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(cwd_socketFd, command) != 0) {
    fatalError("send() command failed");
  }

  uint32_t nameLength;
  if (util_recvU32(cwd_socketFd, &nameLength) != 0) {
    fatalError("failed to get cwd length");
  }

  char *cwd = util_recvLatin1AsUtf8(cwd_socketFd, nameLength);

  if (!cwd) {
    fatalError("failed to get cwd");
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

  const char* hostname = argv[1];

  const char* cwd = cwd_read(hostname);

  if (!cwd) {
    fatalError("cwd failed");
  }

  printf("%s\n", cwd);

  free((void*)cwd);

}
