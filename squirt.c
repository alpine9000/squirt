#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "main.h"
#include "common.h"

static int squirt_fileFd = 0;
static char* squirt_readBuffer = 0;


void
squirt_cleanup(void)
{
  if (squirt_readBuffer) {
    free(squirt_readBuffer);
    squirt_readBuffer = 0;
  }

  if (squirt_fileFd) {
    close(squirt_fileFd);
    squirt_fileFd = 0;
  }
}


int
squirt_file(const char* filename, const char* progressHeader, const char* destFilename, int writeToCurrentDir, void (*progress)(const char* filename, struct timeval* start, uint32_t total, uint32_t fileLength))
{
  int total = 0;
  int32_t fileLength;
  struct stat st;

  struct timeval start, end;

  if (stat(filename, &st) == -1) {
    fatalError("filed to stat %s", filename);
  }

  fileLength = st.st_size;

  if (util_sendCommand(main_socketFd, writeToCurrentDir ? SQUIRT_COMMAND_SQUIRT_TO_CWD : SQUIRT_COMMAND_SQUIRT) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  const char* amigaFilename;

  if (destFilename) {
    amigaFilename = destFilename;
  } else {
    amigaFilename = basename((char*)filename);
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, amigaFilename) != 0) {
    fatalError("send() name failed");
  }

  if (util_sendU32(main_socketFd, fileLength) != 0) {
    fatalError("send() fileLength failed");
  }

  squirt_fileFd = util_open(filename, O_RDONLY|_O_BINARY);

  if (!squirt_fileFd) {
    fatalError("failed to open %s", filename);
  }

  squirt_readBuffer = malloc(BLOCK_SIZE);

  if (progress == util_printProgress) {
    printf("squirting %s (%s bytes)\n", filename, util_formatNumber(fileLength));
    gettimeofday(&start, NULL);
  }

  do {
    int len;
    if ((len = read(squirt_fileFd, squirt_readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("failed to read %s", filename);
    } else {
      if ((send(main_socketFd, squirt_readBuffer, len, 0)) != len) {
	fatalError("send() failed");
      }
      //      int old = total;
      total += len;
      //      if (((((old*100)/fileLength))/100) - (((total*100)/fileLength)/100) > 2) {
      if (progress) {
	progress(progressHeader ? progressHeader : filename, &start, total, fileLength);
      }
	//      }
    }

  } while (total < fileLength);

  if (progress == util_printProgress) {
    util_printProgress(progressHeader ? progressHeader :filename, &start, total, fileLength);
  }

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("squirt: failed to read remote status");
  }

  if (error == 0) {
    if (progress == util_printProgress) {
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

  squirt_cleanup();

  return error;
}


void
squirt_main(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname filename", main_argv0);
  }

  util_connect(argv[1]);
  squirt_file(argv[2], 0, 0, 0, util_printProgress);
}
