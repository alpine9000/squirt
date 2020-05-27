#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef AMIGA
#include <proto/exec.h>
#include <proto/socket.h>
#include <proto/dos.h>
#else
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "common.h"
#include "squirtd.h"


uint32_t squirtd_error;
static int socketFd = 0;
static int acceptFd = 0;
static char* filename = 0;
static char* rxBuffer = 0;
static file_handle_t outputFd = 0;
static file_handle_t inputFd = 0;


static void
cleanupForNextRun(void)
{
  if (inputFd > 0) {
    Close(inputFd);
    inputFd = 0;
  }

  if (acceptFd > 0) {
    CloseSocket(acceptFd);
    acceptFd = 0;
  }

  if (rxBuffer) {
    free(rxBuffer);
    rxBuffer = 0;
  }

  if (filename) {
    free(filename);
    filename = 0;
  }

  if (outputFd > 0) {
    Close(outputFd);
    outputFd = 0;
  }
}


static void
cleanup(void)
{
  if (socketFd > 0) {
    CloseSocket(socketFd);
    socketFd = 0;
  }

  cleanupForNextRun();
}


#ifdef AMIGA
static void
_fatalError(void)
#else
static void
_fatalError(const char *msg)
#endif
{
  fprintf(stderr, "%s", msg);
  cleanup();
  exit(1);
}

int16_t
file_get(const char* destFolder, uint32_t nameLength)
{
  int destFolderLen = strlen(destFolder);
  int fullPathLen = nameLength+destFolderLen;
  filename = malloc(fullPathLen+1);
  strcpy(filename, destFolder);

  if (recv(acceptFd, filename+destFolderLen, nameLength, 0) != nameLength) {
    return ERROR_RECV_FAILED;
  }

  filename[fullPathLen] = 0;

  int32_t fileLength;
  if (recv(acceptFd, (void*)&fileLength, sizeof(fileLength), 0) != sizeof(fileLength)) {
    return  ERROR_RECV_FAILED;
  }

  fileLength = ntohl(fileLength);

  DeleteFile(filename);

  if ((outputFd = Open(filename, MODE_NEWFILE)) == 0) {
    return ERROR_CREATE_FILE_FAILED;
  }

  rxBuffer = malloc(BLOCK_SIZE);
  int total = 0, timeout = 0, length;
  do {
    if ((length = recv(acceptFd, (void*)rxBuffer, BLOCK_SIZE, 0)) < 0) {
      return ERROR_RECV_FAILED;
    }
    if (length) {
      total += length;
      if (Write(outputFd, rxBuffer, length) != length) {
	return ERROR_FILE_WRITE_FAILED;
      }
      timeout = 0;
    } else {
      timeout++;
    }
  } while (timeout < 2 && total < fileLength);

  printf("got %s -> %d\n", filename, total);

  return 0;
}


int16_t
file_send(void)
{
#ifdef AMIGA
  printf("sending %s\n", filename);

  BPTR lock = Lock(filename, ACCESS_READ);
  if (!lock) {
    return ERROR_FILE_READ_FAILED;
  }

  struct FileInfoBlock fileInfo;
  Examine(lock, &fileInfo);
  UnLock(lock);
  uint32_t fileSize = fileInfo.fib_Size;
  uint32_t fileNameLength = strlen(filename);

  if (send(acceptFd, (void*)&fileSize, sizeof(fileSize), 0) != sizeof(fileSize)) {
    return ERROR_SEND_FAILED;
  }

  if (fileSize == 0) {
    return 0;
  }

  inputFd = Open(filename, MODE_OLDFILE);

  if (!inputFd) {
    printf("open failed %s\n", filename);
    return ERROR_FILE_READ_FAILED;
  }

  rxBuffer = malloc(BLOCK_SIZE);

  int total = 0;
  do {
    int len;
    if ((len = Read(inputFd, rxBuffer, BLOCK_SIZE) ) < 0) {
      return ERROR_FILE_READ_FAILED;
    } else {
      if (send(acceptFd, rxBuffer, len, 0) != len) {
	  return ERROR_SEND_FAILED;
      }
      total += len;
    }

  } while (total < fileSize);

#endif
  return 0;
}

int
main(int argc, char **argv)
{
  if (argc != 2) {
    fatalError("squirtd: dest_folder\n");
  }

  struct sockaddr_in sa = {0};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = 0; //inet_addr("0.0.0.0");
  sa.sin_port = htons(NETWORK_PORT);

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  const int ONE = 1;
  setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(socketFd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    fatalError("bind() failed\n");
  }

  if (listen(socketFd, 1)) {
    fatalError("listen() failed\n");
  }


 again:
  printf("restarting\n");

  squirtd_error = 0;

  if ((acceptFd = accept(socketFd, 0, 0)) == -1) {
    fatalError("accept() failed\n");
  }

  LONG socketTimeout = 1000;
  setsockopt(acceptFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&socketTimeout, sizeof(socketTimeout));

  uint8_t command;
  if (recv(acceptFd, (void*)&command, sizeof(command), 0) != sizeof(command)) {
    squirtd_error = ERROR_RECV_FAILED;
    goto cleanup;
  }

  uint32_t nameLength;
  if (recv(acceptFd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength)) {
    squirtd_error = ERROR_RECV_FAILED;
    goto cleanup;
  }

  nameLength = ntohl(nameLength);

  if (command != SQUIRT_COMMAND_SQUIRT) {
    filename = malloc(nameLength+1);

    if (recv(acceptFd, filename, nameLength, 0) != nameLength) {
      squirtd_error = ERROR_RECV_FAILED;
      goto cleanup;
    }

    filename[nameLength] = 0;

    if (command == SQUIRT_COMMAND_CLI) {
      exec_run(filename, acceptFd);
    } else if (command == SQUIRT_COMMAND_CD) {
      exec_cd(filename, acceptFd);
    } else if (command == SQUIRT_COMMAND_SUCK) {
      file_send();
    } else if (command == SQUIRT_COMMAND_DIR) {
      exec_dir(filename, acceptFd);
    }
    goto cleanup;
  } else {
    squirtd_error = file_get(argv[1], nameLength);
    if (squirtd_error) {
      goto cleanup;
    }
  }

 cleanup:
  squirtd_error = htonl(squirtd_error);
  send(acceptFd, (void*)&squirtd_error, sizeof(squirtd_error), 0);
  cleanupForNextRun();

  goto again;

  return 0;
}
