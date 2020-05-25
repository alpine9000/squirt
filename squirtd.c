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

#include "squirt.h"

#ifdef AMIGA
typedef BPTR file_handle_t;
#define SOCKLEN_T LONG
#define printf(...)
#define fprintf(...)
#define fatalError(x) _fatalError()
#else
typedef int file_handle_t;
typedef int32_t LONG;
#define fatalError(x) _fatalError(x)
#define DeleteFile(x)
#define Open(x, y) open(x, y, 0777)
#define Close(x) close(x)
#define CloseSocket(x) close(x)
#define Write(x, y, z) write(x, y, z)
#define SOCKLEN_T socklen_t
#define MODE_READWRITE (O_WRONLY|O_CREAT)
#define MODE_NEWFILE (O_WRONLY|O_CREAT)
#endif

static const char* destFolder;
static int socketFd = 0;
static int acceptFd = 0;
static file_handle_t outputFd = 0;
static char* filename = 0;
static char* rxBuffer = 0;

static void
cleanupForNextRun(void)
{
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
  exit(0);
}


int main(int argc, char **argv)
{
  const int ONE = 1;
  uint32_t fileLength;
  uint32_t nameLength;
  struct sockaddr_in sa, isa;
  const LONG addr_size = sizeof(isa);

  if (argc != 2) {
    fatalError("squirtd: dest_folder\n");
  }

  destFolder = argv[1];

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = 0; //inet_addr("0.0.0.0");
  sa.sin_port = htons(NETWORK_PORT);

  socketFd = socket(AF_INET, SOCK_STREAM, 0);

  if (socketFd < 0) {
    fatalError("socket() failed\n");
  }

  setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(socketFd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    fatalError("bind() failed\n");
  }

  if (listen(socketFd, 1)) {
    fatalError("listen() failed\n");
  }

  int32_t error;

 again:

  error = 0;

  if ((acceptFd = accept(socketFd, (struct sockaddr*)&isa, (SOCKLEN_T*)&addr_size)) == -1) {
    fatalError("accept() failed\n");
  }

  LONG socketTimeout = 1000;
  setsockopt(acceptFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&socketTimeout, sizeof(socketTimeout));

  if (recv(acceptFd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength)) {
    error = ERROR_RECV_FAILED_READING_NAME_LENGTH;
    goto cleanup;
  }

  int destFolderLen = strlen(destFolder);
  nameLength = ntohl(nameLength);
  filename = malloc(nameLength+destFolderLen+1);
  strcpy(filename, destFolder);

  if (recv(acceptFd, filename+destFolderLen, nameLength, 0) != nameLength) {
    error = ERROR_RECV_FAILED_READING_FILENAME;
    goto cleanup;
  }

  filename[nameLength+destFolderLen] = 0;

  if (recv(acceptFd, (void*)&fileLength, sizeof(fileLength), 0) != sizeof(fileLength)) {
    error = ERROR_RECV_FAILED_READING_FILE_LENGTH;
    goto cleanup;
  }

  fileLength = ntohl(fileLength);

  DeleteFile(filename);
  outputFd = Open(filename, MODE_NEWFILE);

  if (!outputFd) {
    error = ERROR_FAILED_TO_CREATE_DESTINATION_FILE;
    goto cleanup;
  }

  rxBuffer = malloc(BLOCK_SIZE);
  int total = 0, timeout = 0;
  int length;
  do {
    if ((length = recv(acceptFd, (void*)rxBuffer, BLOCK_SIZE, 0)) < 0) {
      error = ERROR_RECV_FAILED_READING_FILE_DATA;
      goto cleanup;
    }
    if (length) {
      total += length;
      if (Write(outputFd, rxBuffer, length) != length) {
	error = ERROR_WRITE_FAILED_WRITING_FILE_DATA;
	goto cleanup;
      }
      timeout = 0;
    } else {
      timeout++;
    }
  } while (timeout < 2 && total < fileLength);

  printf("got %s -> %d\n", filename, total);

 cleanup:
  error = htonl(error);
  send(acceptFd, (void*)&error, sizeof(error), 0);

  cleanupForNextRun();

  goto again;

  cleanup();

  return 0;
}
