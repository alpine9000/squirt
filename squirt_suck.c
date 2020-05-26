#include <ncurses.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <locale.h>
#include <libgen.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "squirt.h"
#include "common.h"

static int socketFd = 0;
static int fileFd = 0;
static char* readBuffer = 0;
static int screenWidth;

static void
cleanupAndExit(void)
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

  exit(0);
}


static void
fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  cleanupAndExit();
}


static void
printFormatSpeed(int32_t size, double elapsed)
{
  double speed = (double)size/elapsed;
  if (speed < 1000) {
    printf("%0.2f bytes/s", speed);
  } else if (speed < 1000000) {
    printf("%0.2f kB/s", speed/1000.0f);
  } else {
    printf("%0.2f MB/s", speed/1000000.0f);
  }
}


static void
printProgress(struct timeval* start, uint32_t total, uint32_t fileLength)
{
  int percentage = (total*100)/fileLength;
  int barWidth = screenWidth - 20;
  int screenPercentage = (percentage*barWidth)/100;
  struct timeval current;

  printf("\r%c[K", 27);
  printf("%02d%% [", percentage);

  for (int i = 0; i < barWidth; i++) {
    if (screenPercentage > i) {
      printf("=");
    } else if (screenPercentage == i) {
      printf(">");
    } else {
      printf(" ");
    }
  }
  printf("] ");

  gettimeofday(&current, NULL);
  long seconds = current.tv_sec - start->tv_sec;
  long micros = ((seconds * 1000000) + current.tv_usec) - start->tv_usec;
  printFormatSpeed(total, ((double)micros)/1000000.0f);
  fflush(stdout);
}


static void
getWindowSize(void)
{
  initscr();
  int ydim;
  getmaxyx(stdscr, ydim, screenWidth);
  endwin();
}


static const char*
amigaBaseName(const char* filename)
{
  int i;
  for (i = strlen(filename)-1; i > 0 && filename[i] != '/' && filename[i] != ':'; --i);
  if (i > 0) {
    return &filename[i+1];
  } else {
    return filename;
  }
}


int
squirt_suck(int argc, char* argv[])
{
  uint32_t total = 0;
  struct sockaddr_in sockAddr;
  struct timeval start, end;

  if (argc != 3) {
    fatalError("usage: %s hostname filename\n", argv[0]);
  }

  const char* filename = argv[2];

  setlocale(LC_NUMERIC, "");
  getWindowSize();

  if (!util_getSockAddr(argv[1], NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }

  uint8_t commandCode = SQUIRT_COMMAND_SUCK;

  if (send(socketFd, &commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    fatalError("send() commandCode failed\n");
  }

  uint32_t nameLength = strlen(filename);
  uint32_t networkNameLength = htonl(nameLength);

  if (send(socketFd, &networkNameLength, sizeof(networkNameLength), 0) != sizeof(networkNameLength)) {
    fatalError("send() nameLength failed\n");
  }


  if (send(socketFd, filename, nameLength, 0) != nameLength) {
    fatalError("send() filename failed\n");
  }

  uint32_t networkFileLength;

  if (recv(socketFd, &networkFileLength, sizeof(networkFileLength), 0) != sizeof(networkFileLength)) {
    fatalError("recv() fileLength failed\n");
  }

  uint32_t fileLength = ntohl(networkFileLength);
  const char* baseName = amigaBaseName(filename);

  if (fileLength == 0) {
    fatalError("%s: failed to suck file %s\n", argv[0], filename);
  }

  fileFd = open(baseName, O_WRONLY|O_CREAT, 0777);

  if (!fileFd) {
    fatalError("%s: failed to open %s\n", argv[0], baseName);
  }

  readBuffer = malloc(BLOCK_SIZE);

  printf("sucking %s (%'d bytes)\n", filename, fileLength);

  fflush(stdout);

  gettimeofday(&start, NULL);

  do {
    int len;
    if ((len = read(socketFd, readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("%s failed to read\n", argv[0]);
    } else {
      printProgress(&start, total, fileLength);
      if (write(fileFd, readBuffer, len) != len) {
	fatalError("%s failed to write to %s\n", argv[0], baseName);
      }
      total += len;
    }

  } while (total < fileLength);

  printProgress(&start, total, fileLength);

  gettimeofday(&end, NULL);
  long seconds = end.tv_sec - start.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
  printf("\nsucked %s -> %s (%'d bytes) in %0.02f seconds ", filename, baseName, fileLength, ((double)micros)/1000000.0f);
  printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
  printf("\n");


  cleanupAndExit();

  return 0;
}
