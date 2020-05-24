#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <proto/dos.h>

static const int BLOCK_SIZE = 8192;
static const char* destFolder;
static int socket_fd = 0;
static int accept_fd = 0;
static BPTR output_fd = 0;
static char* filename = 0;
static char * rx_buffer = 0;

static void
cleanupForNextRun(void)
{
  if (rx_buffer) {
    free(rx_buffer);
    rx_buffer = 0;
  }

  if (filename) {
    free(filename);
    filename = 0;
  }

  if (output_fd) {
    Close(output_fd);
    output_fd = 0;
  }

  if (accept_fd) {
    CloseSocket(accept_fd);
    accept_fd = 0;
  }
}


static void
cleanupAndExit(void)
{
  cleanupForNextRun();

  if (socket_fd) {
    CloseSocket(socket_fd);
  }

  exit(0);
}


int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "%s: dest_folder\n", argv[0]);
    exit(1);
  }

  destFolder = argv[1];
  printf("squirt something at me!\n");

  uint32_t fileLength;
  uint32_t nameLength;
  const int port = 6969;
  const int ONE = 1;
  LONG addr_size;
  struct sockaddr_in sa, isa;

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = inet_addr("0.0.0.0");
  sa.sin_port = htons(port);

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_fd < 0) {
    fprintf(stderr, "socket() failed\n");
    cleanupAndExit();
  }

  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    fprintf(stderr, "bind() failed\n");
    cleanupAndExit();
  }

  if (listen(socket_fd, 5)) {
    fprintf(stderr, "listen() failed\n");
    cleanupAndExit();
  }

  addr_size = sizeof(isa);

 again:

  if ((accept_fd = accept(socket_fd, (struct sockaddr*)&isa, &addr_size)) == -1) {
    fprintf(stderr, "accept() failed\n");
    cleanupAndExit();
  }

  if (recv(accept_fd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength)) {
    fprintf(stderr, "recv() failed to read name length\n");
    cleanupAndExit();
  }

  filename = malloc(nameLength+strlen(destFolder)+1);
  strcpy(filename, destFolder);

  if (recv(accept_fd, filename+strlen(destFolder), nameLength, 0) != nameLength) {
    fprintf(stderr, "recv() failed to read name\n");
    cleanupAndExit();
  }

  filename[nameLength+strlen(destFolder)] = 0;

  if (recv(accept_fd, (void*)&fileLength, sizeof(fileLength), 0) != sizeof(fileLength)) {
    fprintf(stderr, "recv() failed to read file length\n");
    cleanupAndExit();
  }

  output_fd = Open(filename, MODE_READWRITE);

  if (!output_fd) {
    fprintf(stderr, "failed to open %s\n", filename);
    cleanupAndExit();
  }

  rx_buffer = malloc(BLOCK_SIZE);
  int total = 0;
  do {
    int length;
    if ((length = recv(accept_fd, (void*)rx_buffer, BLOCK_SIZE, 0)) == 0) {
      fprintf(stderr, "recv() failed\n");
      cleanupAndExit();
    }
    total += length;
    if (Write(output_fd, rx_buffer, length) != length) {
      fprintf(stderr, "write() failed\n");
      cleanupAndExit();
    }
  } while (total < fileLength);

  printf("got %s -> %d\n", filename, total);

  cleanupForNextRun();

  goto again;

  cleanupAndExit();

  return 0;
}
