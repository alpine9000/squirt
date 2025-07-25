#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "argv.h"
#include "main.h"
#include "common.h"

static char* exec_command = 0;


void
exec_cleanup(void)
{
  if (exec_command) {
    free(exec_command);
    exec_command = 0;
  }
}


int
exec_cmd(int argc, char** argv)
{
  uint8_t commandCode;
  int commandLength = 0;
  exec_command = 0;

  if (argc == 2 && strcmp("cd", argv[0]) == 0) {
    commandLength = strlen(argv[1]);
    exec_command = malloc(commandLength+1);
    strcpy(exec_command, argv[1]);
    commandCode = SQUIRT_COMMAND_CD;
  } else {
    for (int i = 0; i < argc; i++) {
      commandLength += strlen(argv[i]);
      commandLength++;
    }

    exec_command = malloc(commandLength+1);
    strcpy(exec_command, argv[0]);
    for (int i = 1; i < argc; i++) {
      strcat(exec_command, " ");
      strcat(exec_command, argv[i]);
    }
    commandCode = SQUIRT_COMMAND_CLI;
  }

  if (util_sendCommand(main_socketFd, commandCode) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, exec_command) != 0) {
    fatalError("send() command failed");
  }

  if (commandCode != SQUIRT_COMMAND_CD) {
    uint8_t c;
    char buffer[1024];
    int bindex = 0;
    int exitState = 0;
    while (util_recv(main_socketFd, &c, 1, 0) == 1) {
      if (c == 0) {
	exitState++;
	if (exitState == 4) {
	  break;
	}
      } else if (c == 0x9B) {
	//write(1, buffer, bindex);
	buffer[bindex] = 0;
	char* utf8 = util_latin1ToUtf8(buffer);
	size_t ignored __attribute__((unused)) = write(1, utf8, strlen(utf8));
	free(utf8);
	bindex = 0;
	fprintf(stdout, "%c[", 27);
	fflush(stdout);
      } else {
	buffer[bindex++] = c;
	if (c == '\n' || bindex == sizeof(buffer)) {
	  //write(1, buffer, bindex);
	  buffer[bindex] = 0;
	  char* utf8 = util_latin1ToUtf8(buffer);
	  size_t ignored __attribute__((unused)) = write(1, utf8, strlen(utf8));
	  free(utf8);
	  bindex = 0;
	}
      }
    }

    if (bindex) {
      size_t ignored __attribute__((unused)) = write(1, buffer, bindex);
    }
  }

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("exec: failed to read remote status");
  }

  exec_cleanup();

  return error;
}


char*
exec_captureCmd(uint32_t* errorCode, int argc, char** argv)
{
  uint8_t commandCode;
  int commandLength = 0;
  exec_command = 0;

  int outputLength = 255;
  int outputSize = 0;
  char* output = malloc(outputLength);

  for (int i = 0; i < argc; i++) {
    commandLength += strlen(argv[i]);
    commandLength++;
  }

  exec_command = malloc(commandLength+1);
  strcpy(exec_command, argv[0]);
  for (int i = 1; i < argc; i++) {
    strcat(exec_command, " ");
    strcat(exec_command, argv[i]);
  }
  commandCode = SQUIRT_COMMAND_CLI;

  if (util_sendCommand(main_socketFd, commandCode) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, exec_command) != 0) {
    fatalError("send() command failed");
  }

  if (commandCode != SQUIRT_COMMAND_CD) {
    uint8_t c;
    int exitState = 0;
    while (util_recv(main_socketFd, &c, 1, 0) == 1) {
      if (c == 0) {
        exitState++;
        if (exitState == 4) {
          if (outputSize >= outputLength) {
            outputLength = outputLength*2;
            output = realloc(output, outputLength);
          }
          output[outputSize++] = 0;
          break;
        }
      } else {
	if (outputSize >= outputLength) {
	  outputLength = outputLength*2;
	  output = realloc(output, outputLength);
	}
	output[outputSize++] = c;
      }
    }
  }

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("exec: failed to read remote status");
  }

  exec_cleanup();

  *errorCode = error;
  return output;
}


void
exec_main(int argc, char* argv[])
{
  if (argc < 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname command to be executed", argv[0]);
  }

  util_connect(argv[1]);

  for (int i = 0; i < argc-2; i++) {
    argv[i] = argv[i+2];
  }

  argc-=2;

  uint32_t error = exec_cmd(argc, argv);

  if (error != 0) {
    fatalError("%s", util_getErrorString(error));
  }
}
