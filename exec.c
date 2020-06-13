#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#endif
#include "argv.h"
#include "main.h"
#include "common.h"

static char* exec_command = 0;
static int exec_exitState = 0;
static volatile int exec_sigIntActive = 0;

void
exec_onCtrlC(void)
{
  printf(" Interrupt\n");
  exec_sigIntActive = 1;
  util_onCtrlC(exec_onCtrlC); 
}


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
  exec_sigIntActive = 0;
  exec_exitState = 0;

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
#ifdef _WIN32
    char buffer[20];
    int bindex = 0;
#endif

    fd_set readFds;

    int done = 0;
    while (!done) {
      FD_ZERO(&readFds);
      FD_SET(main_socketFd, &readFds);
      int numfds;
#ifdef _WIN32
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 500000;
      numfds = select(FD_SETSIZE, &readFds, 0, 0, &tv);
#else
      sigset_t sig = {0};
      numfds = pselect(FD_SETSIZE, &readFds, 0, 0, 0, &sig);
#endif

      if (exec_sigIntActive) {
	send(main_socketFd, (void*)&c, 1, 0);
	exec_sigIntActive = 0;
      }

      if (numfds && FD_ISSET(main_socketFd, &readFds)) {
	int recvd = util_recv(main_socketFd, &c, sizeof(c), 0);
	if (recvd <= 0) {
	  return -1;
	}

	if (c == 0) {
	  exec_exitState++;
	  fflush(stdout);

	  if (exec_exitState == 4) {
	    done = 1;
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
    }


#ifdef _WIN32
    if (bindex) {
      write(1, buffer, bindex);
    }
#endif
  }

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("exec: failed to read remote status");
  }

  exec_cleanup();

  return error;
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
