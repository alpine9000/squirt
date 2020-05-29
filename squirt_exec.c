#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "squirt.h"
#include "common.h"

static int socketFd = 0;
static char* command = 0;


static void
cleanupAndExit(int errorCode)
{
  if (command) {
    free(command);
  }

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
  fprintf(stderr, "%s: ", squirt_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  cleanupAndExit(EXIT_FAILURE);
}


int
squirt_cli(int argc, char* argv[])
{
  struct sockaddr_in sockAddr;

  if (argc < 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname command to be executed", argv[0]);
  }

  uint8_t commandCode;
  int commandLength = 0;
  socketFd = 0;
  command = 0;

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
    fatalError("getSockAddr() failed");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed");
  }


  if (send(socketFd, (const void*)&commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed");
  }

  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, command)) {
    fatalError("send() command failed");
  }

  uint8_t c;
#ifdef _WIN32
  char buffer[20];
  int bindex = 0;
#endif
  while (util_recv(socketFd, &c, 1, 0)) {
    if (c == 0) {
      break;
    } else if (c == 0x9B) {
      fprintf(stdout, "%c[", 27);
      fflush(stdout);
    } else {
#ifdef _WIN32
      buffer[bindex++] = c;
      if (bindex == sizeof(buffer)) {
	write(1, buffer, bindex);
	bindex = 0;
      }
#else
      int ignore = write(1, &c, 1);
      (void)ignore;
#endif
    }
  }

#ifdef _WIN32
  if (bindex) {
    write(1, buffer, bindex);
  }
#endif

  uint32_t error;

  if (util_recv(socketFd, &error, sizeof(error), 0) != sizeof(error)) {
    fatalError("failed to read remote status");
  }

  error = ntohl(error);

  if (ntohl(error) != 0) {
    fatalError("%s", util_getErrorString(error));
  }

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
