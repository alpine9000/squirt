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


static void
cleanupAndExit(uint32_t exitCode)
{
  cleanup();
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
squirt_file(const char* hostname, const char* filename, const char* destFilename, int writeToCurrentDir, int progress)
{
  int total = 0;
  int32_t fileLength;
  struct stat st;

  struct timeval start, end;

  if (stat(filename, &st) == -1) {
    fatalError("filed to stat %s", filename);
  }

  fileLength = st.st_size;

  if ((socketFd = util_connect(hostname, writeToCurrentDir ? SQUIRT_COMMAND_SQUIRT_TO_CWD : SQUIRT_COMMAND_SQUIRT)) < 0) {
    fatalError("failed to connect to squirtd server");
  }

  const char* amigaFilename;

  if (destFilename) {
    amigaFilename = destFilename;
  } else {
    amigaFilename = basename((char*)filename);
  }

  if (util_sendLengthAndUtf8StringAsLatin1(socketFd, amigaFilename) != 0) {
    fatalError("send() name failed");
  }

  if (util_sendU32(socketFd, fileLength) != 0) {
    fatalError("send() fileLength failed");
  }


  fileFd = util_open(filename, O_RDONLY);

  if (!fileFd) {
    fatalError("failed to open %s", filename);
  }

  readBuffer = malloc(BLOCK_SIZE);

  if (progress) {
    printf("squirting %s (%s bytes)\n", filename, util_formatNumber(fileLength));
    gettimeofday(&start, NULL);
  }

  do {
    int len;
    if ((len = read(fileFd, readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("failed to read %s", filename);
    } else {
      if ((send(socketFd, readBuffer, len, 0)) != len) {
	fatalError("send() failed");
      }
      //      int old = total;
      total += len;
      //      if (((((old*100)/fileLength))/100) - (((total*100)/fileLength)/100) > 2) {
      if (progress) {
	util_printProgress(&start, total, fileLength);
      }
	//      }
    }

  } while (total < fileLength);

  if (progress) {
    util_printProgress(&start, total, fileLength);
  }

  uint32_t error;

  if (util_recvU32(socketFd, &error) != 0) {
    fatalError("failed to read remote status");
  }

  if (error == 0) {
    if (progress) {
      gettimeofday(&end, NULL);
      long seconds = end.tv_sec - start.tv_sec;
      long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
      printf("\nsquirted %s (%s bytes) in %0.02f seconds ", filename, util_formatNumber(fileLength), ((double)micros)/1000000.0f);
      util_printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
      printf("\n");
    }
  } else {
    fprintf(stderr, "\n**FAILED** to squirt %s\n%s\n", filename, util_getErrorString(error));
  }

  return error;
}

int
squirt(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", squirt_argv0);
  }

  squirt_file(argv[1], argv[2], 0, 0, 1);

  cleanupAndExit(EXIT_SUCCESS);

  return 0;
}
