#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/stat.h>

static const int BLOCK_SIZE = 8192;
static int socketFd = 0;
static int fileFd = 0;
static char* readBuffer = 0;

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
  int screenPercentage = (percentage*60)/100;
  struct timeval current;

  printf("\r%c[K", 27);
  printf("%02d%% [", percentage);
  //  printf("[");
  for (int i = 0; i < 60; i++) {
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

int
main(int argc, char* argv[])
{
  const int ONE = 1;
  int total = 0;
  int32_t fileLength;
  struct stat st;
  struct sockaddr_in sockAddr;
  struct timeval start, end;

  if (argc != 3) {
    fatalError("usage: %s filename hostname\n", argv[0]);
  }

  if (stat(argv[1], &st) == -1) {
    fatalError("%s: filed to stat %s\n", argv[0], argv[1]);
  }

  fileLength = st.st_size;

  if (!getSockAddr(argv[2], 6969, &sockAddr)) {
    fatalError("getSockAddr() failed\n");
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  setsockopt(socketFd, SOL_SOCKET, SO_KEEPALIVE, (void*)&ONE, sizeof ONE);

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fatalError("connect() failed\n");
  }

  int32_t nameLength = strlen(argv[1]);
  int32_t networkNameLength = htonl(nameLength);

  if (send(socketFd, &networkNameLength, sizeof(networkNameLength), 0) != sizeof(nameLength)) {
    fatalError("send() nameLength failed\n");
  }

  if (send(socketFd, argv[1], nameLength, 0) != nameLength) {
    fatalError("send() name failed\n");
  }

  int32_t networkFileLength = htonl(fileLength);

  if (send(socketFd, &networkFileLength, sizeof(networkFileLength), 0) != sizeof(fileLength)) {
    fatalError("send() fileLength failed\n");
  }

  fileFd = open(argv[1], O_RDONLY);

  if (!fileFd) {
    fatalError("%s: failed to open %s\n", argv[0], argv[1]);
  }

  readBuffer = malloc(BLOCK_SIZE);

  printf("squirting %s (%d bytes)\n", argv[1], fileLength);

  gettimeofday(&start, NULL);

  do {
    int len;
    if ((len = read(fileFd, readBuffer, BLOCK_SIZE) ) < 0) {
      fatalError("%s failed to read %s\n", argv[0], argv[1]);
    } else {
      printProgress(&start, total, fileLength);
      if (send(socketFd, readBuffer, len, 0) != len) {
	fatalError("send() failed\n");
      }
      total += len;
    }
  } while (total < fileLength);

  printProgress(&start, total, fileLength);

  gettimeofday(&end, NULL);

  long seconds = end.tv_sec - start.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
  printf("\nsquirted %d bytes in %0.02f seconds ", fileLength, ((double)micros)/1000000.0f);
  printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
  printf("\n");

  cleanupAndExit();

  return 0;
}
