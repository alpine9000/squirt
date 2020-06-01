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

  if (socketFd > 0) {
    close(socketFd);
    socketFd = 0;
  }

  if (fileFd) {
    close(fileFd);
    fileFd = 0;
  }
}


static _Noreturn void
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


int32_t
squirt_suckFile(const char* hostname, const char* filename, int progress, const char* destFilename)
{
  int32_t total = 0;

  fflush(stdout);

  if ((socketFd = util_connect(hostname, SQUIRT_COMMAND_SUCK)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(socketFd, filename) != 0) {
    fatalError("send() filename failed");
  }


  int32_t fileLength;
  if (util_recv32(socketFd, &fileLength) != 0) {
    fatalError("util_recv() Filelength failed");
  }

  const char* baseName;

  if (!destFilename) {
    baseName = util_amigaBaseName(filename);
  } else {
    baseName = destFilename;
  }

  fileFd = open(baseName, O_WRONLY|O_CREAT|O_TRUNC|_O_BINARY, 0777);

  if (!fileFd) {
    fatalError("failed to open %s", baseName);
  }

  readBuffer = malloc(BLOCK_SIZE);

  if (fileLength > 0) {
    if (progress) {
      printf("sucking %s (%s bytes)\n", filename, util_formatNumber(fileLength));
    }

    fflush(stdout);

    gettimeofday(&squirt_suckStart, NULL);

    do {
      int len, requestLength;
      if (fileLength - total > BLOCK_SIZE) {
	requestLength = BLOCK_SIZE;
      } else {
	requestLength = fileLength - total;
      }
      if ((len = util_recv(socketFd, readBuffer, requestLength, 0)) < 0) {
	fflush(stdout);
	fatalError("\n%s failed to read", squirt_argv0);
      } else {
	if (progress) {
	  util_printProgress(&squirt_suckStart, total, fileLength);
	}
	int readLen;
	if ((readLen = write(fileFd, readBuffer, len)) != len) {
	  fflush(stdout);
	  fatalError("\nailed to write to %s %d",  baseName, readLen);
	}
	total += len;
      }
    } while (total < fileLength);

    if (progress) {
      util_printProgress(&squirt_suckStart, total, fileLength);
      fflush(stdout);
    }
  } else {
    total = fileLength;
  }


  uint32_t error;
  if (util_recvU32(socketFd, &error) != 0) {
    fatalError("suck: failed to read remote status");
  }

  if (error) {
    total = -error;
    if (progress) {
      fatalError("failed to suck file %s", filename);
    }
  }

  cleanup();

  return total;
}


int
squirt_suck(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", squirt_argv0);
  }

  int32_t length = squirt_suckFile(argv[1], argv[2], 1, 0);

  struct timeval end;

  gettimeofday(&end, NULL);
  long seconds = end.tv_sec - squirt_suckStart.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - squirt_suckStart.tv_usec;

  const char* baseName = util_amigaBaseName(argv[2]);

  fflush(stdout);

  printf("\nsucked %s -> %s (%s bytes) in %0.02f seconds ", argv[2], baseName, util_formatNumber(length), ((double)micros)/1000000.0f);
  util_printFormatSpeed(length, ((double)micros)/1000000.0f);
  printf("\n");

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
