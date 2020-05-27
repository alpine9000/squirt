#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <locale.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "squirt.h"
#include "common.h"

static int socketFd = 0;

static void
cleanupAndExit(int errorCode)
{
  if (socketFd) {
    close(socketFd);
    socketFd = 0;
  }

  exit(errorCode);
}


static void
fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  cleanupAndExit(EXIT_FAILURE);
}

int
squirt_cli(int argc, char* argv[])
{
  struct sockaddr_in sockAddr;

  if (argc < 3) {
    fatalError("usage: %s hostname command to be executed\n", argv[0]);
  }

  uint8_t commandCode;
  int commandLength = 0;
  char* command = 0;

  if (argc == 4 && strcmp("cd", argv[2]) == 0) {
    commandLength = strlen(argv[3]);
    command = malloc(commandLength+1);
    strcpy(command, argv[3]);
    commandCode = SQUIRT_COMMAND_CD;
  } else {
    for (int i = 2; i < argc; i++) {
      commandLength += strlen(argv[i]);
      commandLength++;
    }

    command = malloc(commandLength+1);
    strcpy(command, argv[2]);
    for (int i = 3; i < argc; i++) {
      strcat(command, " ");
      strcat(command, argv[i]);
    }
    commandCode = SQUIRT_COMMAND_CLI;
  }

  if (!util_getSockAddr(argv[1], NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }


  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed\n");
  }

  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, command)) {
    fatalError("%s: send() command failed\n", squirt_argv0);
  }

  uint8_t c;
  while (recv(socketFd, &c, 1, 0)) {
    if (c == 0) {
      break;
    } else if (c == 0x9B) {
      fprintf(stdout, "%c[", 27);
      fflush(stdout);
    } else {
      write(1, &c, 1);
    }
  }

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("failed to read remote status\n");
  }

  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s: %s\n", argv[0], util_getErrorString(error));
  }


  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
