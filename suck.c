#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include "main.h"
#include "common.h"

static int suck_fileFd = 0;
static char* suck_readBuffer = 0;
static struct timeval suck_start;


void
suck_cleanup(void)
{
  if (suck_readBuffer) {
    free(suck_readBuffer);
    suck_readBuffer = 0;
  }

  if (suck_fileFd) {
    close(suck_fileFd);
    suck_fileFd = 0;
  }
}


int32_t
squirt_suckFile(const char* filename, const char* progressHeader,  void (*progress)(const char* progressHeader, struct timeval* start, uint32_t total, uint32_t fileLength), const char* destFilename, uint32_t* protection)
{
  int32_t total = 0;

  fflush(stdout);

  if (util_sendCommand(main_socketFd, SQUIRT_COMMAND_SUCK) !=  0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, filename) != 0) {
    fatalError("send() filename failed");
  }


  int32_t fileLength;
  if (util_recv32(main_socketFd, &fileLength) != 0) {
    fatalError("util_recv() Filelength failed");
  }


  if (fileLength == -1) {
    uint32_t status;
    util_recvU32(main_socketFd, &status);
    printf("remote file not found %d\n", status);
    return -1;
  }

  if (util_recvU32(main_socketFd, protection) != 0) {
    fatalError("util_recv() protection failed");
  }

  const char* baseName;

  if (!destFilename) {
    baseName = util_amigaBaseName(filename);
  } else {
    baseName = destFilename;
  }

  suck_fileFd = open(baseName, O_WRONLY|O_CREAT|O_TRUNC|_O_BINARY, 0777);

  if (!suck_fileFd) {
    fatalError("failed to open %s", baseName);
  }

  suck_readBuffer = malloc(BLOCK_SIZE);

  if (fileLength > 0) {
    if (progress == util_printProgress) {
      printf("sucking %s (%s bytes)\n", filename, util_formatNumber(fileLength));
    }

    fflush(stdout);

    gettimeofday(&suck_start, NULL);

    do {
      int len, requestLength;
      if (fileLength - total > BLOCK_SIZE) {
	requestLength = BLOCK_SIZE;
      } else {
	requestLength = fileLength - total;
      }
      if ((len = util_recv(main_socketFd, suck_readBuffer, requestLength, 0)) < 0) {
	fflush(stdout);
	fatalError("\nfailed to read");
      } else {
	if (progress) {
	  progress(progressHeader ? progressHeader : filename, &suck_start, total, fileLength);
	}
	int readLen;
	if ((readLen = write(suck_fileFd, suck_readBuffer, len)) != len) {
	  fflush(stdout);
	  fatalError("\nfailed to write to %s %d",  baseName, readLen);
	}
	total += len;
      }
    } while (total < fileLength);

    if (progress) {
      progress(progressHeader ? progressHeader : filename, &suck_start, total, fileLength);
      fflush(stdout);
    }
  } else {
    total = fileLength;
  }


  uint32_t error;
  if (util_recvU32(main_socketFd, &error) != 0) {
    return -1;
  }

  if (error) {
    total = -error;
    if (progress == util_printProgress) {
      fatalError("failed to suck file %s", filename);
    }
  }

  suck_cleanup();

  return total;
}


void
suck_main(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", main_argv0);
  }

  util_connect(argv[1]);

  uint32_t protection;
  int32_t length = squirt_suckFile(argv[2], 0, util_printProgress, 0, &protection);

  struct timeval end;

  gettimeofday(&end, NULL);
  long seconds = end.tv_sec - suck_start.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - suck_start.tv_usec;

  const char* baseName = util_amigaBaseName(argv[2]);

  fflush(stdout);

  if (length > 0) {
    printf("\nsucked %s -> %s (%s bytes) in %0.02f seconds ", argv[2], baseName, util_formatNumber(length), ((double)micros)/1000000.0f);
    util_printFormatSpeed(length, ((double)micros)/1000000.0f);
    printf("\n");
  } else {
    fprintf(stderr, "%s: failed to suck %s\n", main_argv0, argv[2]);
  }
}
