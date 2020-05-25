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

static int socketFd = 0;
static int fileFd = 0;
static int screenWidth;
static char* readBuffer = 0;
static const char* errors[] = {
  [ERROR_SUCCESS] = "Unknown error",
  [ERROR_RECV_FAILED_READING_NAME_LENGTH] = "recv() failed reading name length",
  [ERROR_RECV_FAILED_READING_FILENAME] = "recv() failed reading filename",
  [ERROR_RECV_FAILED_READING_FILE_LENGTH] = "recv() failed reading file length",
  [ERROR_FAILED_TO_CREATE_DESTINATION_FILE] = "Failed to create destination file",
  [ERROR_RECV_FAILED_READING_FILE_DATA] = "recv() failed reading file data",
  [ERROR_WRITE_FAILED_WRITING_FILE_DATA] = "Write() failed writing file data"
};


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


static int
getSockAddr(const char * host, int port, struct sockaddr_in * addr)
{
  struct hostent * remote;

  if ((remote = gethostbyname(host)) != NULL) {
    char **ip_addr;
    memcpy(&ip_addr, &(remote->h_addr_list[0]), sizeof(void *));
    memcpy(&addr->sin_addr.s_addr, ip_addr, sizeof(struct in_addr));
  } else if ((addr->sin_addr.s_addr = inet_addr(host)) == (unsigned long)INADDR_NONE) {
    return 0;
  }

  addr->sin_port = htons(port);
  addr->sin_family = AF_INET;

  return 1;
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
printProgress(struct timeval* start, int total, int fileLength)
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


int
main(int argc, char* argv[])
{
  const int ONE = 1;
  int total = 0;
  int32_t fileLength;
  struct stat st;
  struct sockaddr_in sockAddr;
  struct timeval start, end;
  const char* fullPathname;

  if (argc != 3) {
    fatalError("usage: %s filename hostname\n", argv[0]);
  }

  fullPathname = argv[1];

  if (stat(fullPathname, &st) == -1) {
    fatalError("%s: filed to stat %s\n", argv[0], fullPathname);
  }

  fileLength = st.st_size;

  setlocale(LC_NUMERIC, "");
  getWindowSize();

  if (!getSockAddr(argv[2], NETWORK_PORT, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  setsockopt(socketFd, SOL_SOCKET, SO_KEEPALIVE, (void*)&ONE, sizeof ONE);

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }

  const char* filename = basename((char*)fullPathname);
  int32_t nameLength = strlen(filename);
  int32_t networkNameLength = htonl(nameLength);

  if (send(socketFd, &networkNameLength, sizeof(networkNameLength), 0) != sizeof(nameLength)) {
    fatalError("send() nameLength failed\n");
  }

  if (send(socketFd, filename, nameLength, 0) != nameLength) {
    fatalError("send() name failed\n");
  }

  int32_t networkFileLength = htonl(fileLength);

  if (send(socketFd, &networkFileLength, sizeof(networkFileLength), 0) != sizeof(fileLength)) {
    fatalError("send() fileLength failed\n");
  }

  fileFd = open(fullPathname, O_RDONLY);

  if (!fileFd) {
    fatalError("%s: failed to open %s\n", argv[0], fullPathname);
  }

  readBuffer = malloc(BLOCK_SIZE);

  printf("squirting %s (%'d bytes)\n", filename, fileLength);

  gettimeofday(&start, NULL);

  do {
    int len;
    if ((len = read(fileFd, readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("%s failed to read %s\n", argv[0], fullPathname);
    } else {
      printProgress(&start, total, fileLength);
      if (send(socketFd, readBuffer, len, 0) != len) {
	fatalError("send() failed\n");
      }
      total += len;
    }

  } while (total < fileLength);

  printProgress(&start, total, fileLength);

  uint32_t error;

  if (read(socketFd, &error, sizeof(error)) != sizeof(error)) {
    fatalError("failed to read remote status\n");
  }

  error = ntohl(error);

  if (ntohl(error) == 0) {
    gettimeofday(&end, NULL);
    long seconds = end.tv_sec - start.tv_sec;
    long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
    printf("\nsquirted %s (%'d bytes) in %0.02f seconds ", filename, fileLength, ((double)micros)/1000000.0f);
    printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
    printf("\n");
  } else {
    if (error >= sizeof(errors)) {
      error = 0;
    }
    fprintf(stderr, "\n**FAILED** to squirt %s\n%s\n", filename, errors[error]);
  }

  cleanupAndExit();

  return 0;
}
