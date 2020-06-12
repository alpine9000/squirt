#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "argv.h"
#include "main.h"
#include "common.h"

static char* exec_command = 0;

static int exec_exitState = 0;

int blah_blah = 0;

void
exec_cleanup(void)
{
  if (exec_command) {
    free(exec_command);
    exec_command = 0;
  }
}


static int
exec_readCmdData()
{
  char buffer[1];

  int length;
  ioctl(main_socketFd, FIONREAD, (char*)&length);

  int recvd = util_recv(main_socketFd, &buffer, sizeof(buffer), 0);
  if (recvd <= 0) {
    return -1;
  }

  int done = 0;
  for (int i = 0; i < recvd && !done; i++) {
    uint8_t c = buffer[i];
    if (c == 0) {
      exec_exitState++;
      fflush(stdout);

      if (exec_exitState == 4) {
	done = 1;
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
  if (done) {
    return 1;
  } else {
    return 0;
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
#ifdef _WIN32
    char buffer[20];
    int bindex = 0;
#endif

    fd_set readFds;
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    exec_exitState = 0;

    //    int done = 0;
    // while (!done) {
    // if (
    //   printf("got something\n");
    //   if (1 || FD_ISSET(main_socketFd, &readFds)) {


 //    while (util_recv(main_socketFd, &c, 1, 0)) {
    //    while (select(FD_SETSIZE, &readFds, 0, 0, 0)) {
    int done = 0;
    while (!done) {
      FD_ZERO(&readFds);
      FD_SET(main_socketFd, &readFds);
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      int numfds;
      sigset_t sig = {0};
      numfds = pselect(FD_SETSIZE, &readFds, 0, 0, 0, &sig);

      if (blah_blah) {
	send(main_socketFd, &c, 1, 0);
	blah_blah = 0;
	//	    done = 1;
      }// else {

      if (numfds && FD_ISSET(main_socketFd, &readFds)) {
	done = exec_readCmdData();
      }


      // }
    }

    if (blah_blah) {
      // read 4 byte breakout
      //      uint32_t null;
      //      util_recvU32(main_socketFd, &null);

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
