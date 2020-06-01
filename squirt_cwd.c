#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "argv.h"
#include "squirt.h"
#include "common.h"

static int socketFd = 0;

static void
cleanup(void)
{
  if (socketFd > 0) {
    close(socketFd);
    socketFd = 0;
  }
}


static _Noreturn void
cleanupAndExit(int errorCode)
{
  cleanup();
  exit(errorCode);
}


static void
fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s: ", squirt_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  cleanupAndExit(EXIT_FAILURE);
}


const char*
squirt_cwdRead(const char* hostname)
{
  static const char* command = "cwd";
  socketFd = 0;

  if ((socketFd = util_connect(hostname, SQUIRT_COMMAND_CWD)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(socketFd, command) != 0) {
    fatalError("send() command failed");
  }

  uint32_t nameLength;
  if (util_recvU32(socketFd, &nameLength) != 0) {
    fatalError("failed to get cwd length");
  }

  char *cwd = util_recvLatin1AsUtf8(socketFd, nameLength);

  if (!cwd) {
    fatalError("failed to get cwd");
  }

  cleanup();

  return cwd;
}

int
squirt_cwd(int argc, char* argv[])
{
  if (argc < 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", argv[0]);
  }

  const char* hostname = argv[1];

  const char* cwd = squirt_cwdRead(hostname);

  if (!cwd) {
    fatalError("cwd failed");
  }

  printf("%s\n", cwd);

  free((void*)cwd);

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
