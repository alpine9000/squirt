#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include "squirt.h"
#include "common.h"

static int socketFd = 0;
static int fileFd = 0;
static char* readBuffer = 0;
static struct timeval squirt_suckStart;


static void
cleanup(void)
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
}


static void
cleanupAndExit(uint32_t errorCode)
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


uint32_t
squirt_suckFile(const char* hostname, const char* filename)
{
  uint32_t total = 0;
  struct sockaddr_in sockAddr;

  fflush(stdout);

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed");
  }

  uint8_t commandCode = SQUIRT_COMMAND_SUCK;

  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed");
  }


  if (!util_sendLengthAndUtf8StringAsLatin1(socketFd, filename)) {
    fatalError("send() filename failed");
  }

  uint32_t networkFileLength;

  if (util_recv(socketFd, &networkFileLength, sizeof(networkFileLength), 0) != sizeof(networkFileLength)) {
    fatalError("util_recv() Filelength failed");
  }

  uint32_t fileLength = ntohl(networkFileLength);
  const char* baseName = util_amigaBaseName(filename);

  if (fileLength == 0) {
    //    fatalError("%s: failed to suck file %s\n", squirt_argv0, filename);
  }

  fileFd = open(baseName, O_WRONLY|O_CREAT|O_TRUNC, 0777);

  if (!fileFd) {
    fatalError("failed to open %s", baseName);
  }

  readBuffer = malloc(BLOCK_SIZE);

  printf("sucking %s (%'d bytes)\n", filename, fileLength);

  fflush(stdout);

  gettimeofday(&squirt_suckStart, NULL);

  if (fileLength) {
    do {
      int len, requestLength;
      if (fileLength - total > BLOCK_SIZE) {
	requestLength = BLOCK_SIZE;
      } else {
	requestLength = fileLength - total;
      }
      if ((len = read(socketFd, readBuffer, requestLength) ) < 0) {
	fflush(stdout);
	fatalError("\n%s failed to read", squirt_argv0);
      } else {
	util_printProgress(&squirt_suckStart, total, fileLength);
	int readLen;
	if ((readLen = write(fileFd, readBuffer, len)) != len) {
	  fflush(stdout);
	  fatalError("\nailed to write to %s %d",  baseName, readLen);
	}
	total += len;
      }
    } while (total < fileLength);
  }

  util_printProgress(&squirt_suckStart, total, fileLength);

  fflush(stdout);

  cleanup();

  return total;
}


int
squirt_suck(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", squirt_argv0);
  }

  uint32_t length = squirt_suckFile(argv[1], argv[2]);

  struct timeval end;

  gettimeofday(&end, NULL);
  long seconds = end.tv_sec - squirt_suckStart.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - squirt_suckStart.tv_usec;

  const char* baseName = util_amigaBaseName(argv[2]);

  fflush(stdout);

  printf("\nsucked %s -> %s (%'d bytes) in %0.02f seconds ", argv[2], baseName, length, ((double)micros)/1000000.0f);
  util_printFormatSpeed(length, ((double)micros)/1000000.0f);
  printf("\n");

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
