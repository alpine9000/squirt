#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "argv.h"
#include "squirt.h"
#include "common.h"

static int socketFd = 0;
static char* command = 0;


static void
cleanup(void)
{
  if (command) {
    free(command);
    command = 0;
  }

  if (socketFd > 0) {
    close(socketFd);
    socketFd = 0;
  }
}


static void
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

int
squirt_execCmd(const char* hostname, int argc, char** argv)
{
  uint8_t commandCode;
  int commandLength = 0;
  socketFd = 0;
  command = 0;

  if (argc == 2 && strcmp("cd", argv[0]) == 0) {
    commandLength = strlen(argv[1]);
    command = malloc(commandLength+1);
    strcpy(command, argv[1]);
    commandCode = SQUIRT_COMMAND_CD;
  } else {
    for (int i = 0; i < argc; i++) {
      commandLength += strlen(argv[i]);
      commandLength++;
    }

    command = malloc(commandLength+1);
    strcpy(command, argv[0]);
    for (int i = 1; i < argc; i++) {
      strcat(command, " ");
      strcat(command, argv[i]);
    }
    commandCode = SQUIRT_COMMAND_CLI;
  }

  if ((socketFd = util_connect(hostname, commandCode)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(socketFd, command) != 0) {
    fatalError("send() command failed");
  }

  if (commandCode != SQUIRT_COMMAND_CD) {
    uint8_t c;
#ifdef _WIN32
    char buffer[20];
    int bindex = 0;
#endif
    int exitState = 0;
    while (util_recv(socketFd, &c, 1, 0)) {
      if (c == 0) {
	exitState++;
	if (exitState == 4) {
	  break;
	}
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
  }

  uint32_t error;

  if (util_recvU32(socketFd, &error) != 0) {
    fatalError("failed to read remote status");
  }

  cleanup();

  return error;
}

int
squirt_exec(int argc, char* argv[])
{
  if (argc < 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname command to be executed", argv[0]);
  }

  const char* hostname = argv[1];

  for (int i = 0; i < argc-2; i++) {
    argv[i] = argv[i+2];
  }

  argc-=2;

  uint32_t error = squirt_execCmd(hostname, argc, argv);

  if (error != 0) {
    fatalError("%s", util_getErrorString(error));
  }

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
