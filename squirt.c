#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "squirt.h"
#include "common.h"

static int socketFd = 0;
static int fileFd = 0;
static char* readBuffer = 0;


static void
cleanupAndExit(uint32_t exitCode)
{
  if (readBuffer) {
    free(readBuffer);
    readBuffer = 0;
  }

  if (socketFd) {
    close(socketFd);
    socketFd = 0;
  }

  if (fileFd) {
    close(fileFd);
    fileFd = 0;
  }

  exit(exitCode);
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
squirt(int argc, char* argv[])
{
  int total = 0;
  int32_t fileLength;
  struct stat st;
  struct sockaddr_in sockAddr;
  struct timeval start, end;
  const char* fullPathname;

  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", squirt_argv0);
  }

  fullPathname = argv[2];

  if (stat(fullPathname, &st) == -1) {
    fatalError("filed to stat %s", fullPathname);
  }

  fileLength = st.st_size;

  if (!util_getSockAddr(argv[1], NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed");
  }

  uint8_t commandCode = SQUIRT_COMMAND_SQUIRT;
  if (send(socketFd, (const void*)&commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed");
  }

  const char* filename = basename((char*)fullPathname);

  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, filename)) {
    fatalError("send() name failed");
  }

  int32_t networkFileLength = htonl(fileLength);

  if (send(socketFd, (const void*)&networkFileLength, sizeof(networkFileLength), 0) != sizeof(fileLength)) {
    fatalError("send() fileLength failed");
  }

  fileFd = util_open(fullPathname, O_RDONLY);

  if (!fileFd) {
    fatalError("failed to open %s", fullPathname);
  }

  readBuffer = malloc(BLOCK_SIZE);

  printf("squirting %s (%s bytes)\n", filename, util_formatNumber(fileLength));

  gettimeofday(&start, NULL);

  do {
    int len;
    if ((len = read(fileFd, readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("failed to read %s", fullPathname);
    } else {
      if ((send(socketFd, readBuffer, len, 0)) != len) {
	fatalError("send() failed");
      }
      int old = total;
      total += len;
      if (((((old*100)/fileLength))/100) - (((total*100)/fileLength)/100) > 2) {
	util_printProgress(&start, total, fileLength);
      }
    }

  } while (total < fileLength);

  util_printProgress(&start, total, fileLength);

  uint32_t error;

  if (util_recv(socketFd, &error, sizeof(error), 0) != sizeof(error)) {
    fatalError("failed to read remote status");
  }

  error = ntohl(error);

  if (ntohl(error) == 0) {
    gettimeofday(&end, NULL);
    long seconds = end.tv_sec - start.tv_sec;
    long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
    printf("\nsquirted %s (%s bytes) in %0.02f seconds ", filename, util_formatNumber(fileLength), ((double)micros)/1000000.0f);
    util_printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
    printf("\n");
  } else {
    fprintf(stderr, "\n**FAILED** to squirt %s\n%s\n", filename, util_getErrorString(error));
  }

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
