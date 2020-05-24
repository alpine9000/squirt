#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/stat.h>

static int socket_fd = 0;
static int file_fd = 0;
static char* read_buffer = 0;

static void
cleanupAndExit(void)
{
  if (read_buffer) {
    free(read_buffer);
    read_buffer = 0;
  }

  if (socket_fd) {
    close(socket_fd);
    socket_fd = 0;
  }

  if (file_fd) {
    close(file_fd);
    file_fd = 0;
  }

  exit(0);
}

static int
getSockAddr(const char * host, int port, struct sockaddr_in * addr)
{
  struct hostent * remote;

  if ((remote = gethostbyname(host)) != NULL) {
    char **ip_addr;
    memcpy(&ip_addr, &(remote->h_addr_list[0]), sizeof(void *));
    memcpy(&addr->sin_addr.s_addr, ip_addr, sizeof(struct in_addr));
  } else if ((addr->sin_addr.s_addr = inet_addr(host)) == (unsigned long)INADDR_NONE) { /* -1 */
    return 0;
  }

  addr->sin_port = htons(port);
  addr->sin_family = AF_INET;

  return 1;
}

void
printFormatSpeed(int32_t size, double elapsed)
{
  double speed = (double)size/elapsed;
  if (speed < 1024) {
    printf("%0.2f bytes/s", speed);
  } else {
    printf("%0.2f kb/s", speed/1024.0f);
  }
}

int
main(int argc, char* argv[])
{
  const int ONE = 1;

  struct stat st;
  int32_t fileLength;

  if (argc != 3) {
    fprintf(stderr, "usage: %s file hostname\n", argv[0]);
    cleanupAndExit();
  }

  if (stat(argv[1], &st) == -1) {
    fprintf(stderr, "%s: filed to stat %s\n", argv[0], argv[1]);
    cleanupAndExit();
  }

  fileLength = st.st_size;

  struct sockaddr_in sockAddr;

  if (!getSockAddr(argv[2], 6969, &sockAddr)) {
    fprintf(stderr, "getSockAddr() failed\n");
    cleanupAndExit();
  }


  if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fprintf(stderr, "socket() failed\n");
    cleanupAndExit();
  }

  setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&ONE, sizeof ONE);

  if (connect(socket_fd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    fprintf(stderr, "connect() failed\n");
    cleanupAndExit();
  }

  int32_t nameLength = strlen(argv[1]);
  int32_t networkNameLength = htonl(nameLength);
  if (send(socket_fd, &networkNameLength, sizeof(networkNameLength), 0) != sizeof(nameLength)) {
    fprintf(stderr, "send() nameLength failed\n");
    cleanupAndExit();
  }


  if (send(socket_fd, argv[1], nameLength, 0) != nameLength) {
    fprintf(stderr, "send() name failed\n");
    cleanupAndExit();
  }

  int32_t networkFileLength = htonl(fileLength);
  if (send(socket_fd, &networkFileLength, sizeof(networkFileLength), 0) != sizeof(fileLength)) {
    fprintf(stderr, "send() fileLength failed\n");
    cleanupAndExit();
  }

  file_fd = open(argv[1], O_RDONLY);

  if (!file_fd) {
    fprintf(stderr, "%s: failed to open %s\n", argv[0], argv[1]);
    cleanupAndExit();
  }

  const int BLOCK_SIZE = 8192;
  read_buffer = malloc(BLOCK_SIZE);
  int total = 0;
  struct timeval start, end;

  printf("squirting %s (%d bytes)\n", argv[1], fileLength);

  gettimeofday(&start, NULL);

  do {
    int len;
    if ((len = read(file_fd, read_buffer, BLOCK_SIZE) ) < 0) {
      fprintf(stderr, "%s failed to read %s\n", argv[0], argv[1]);
      cleanupAndExit();
    } else {
      printf("#");
      fflush(stdout);
      if (send(socket_fd, read_buffer, len, 0) != len) {
	perror("send() failed\n");
	cleanupAndExit();
      }
      total += len;
    }
  } while (total < fileLength);


  gettimeofday(&end, NULL);

  long seconds = end.tv_sec - start.tv_sec;
  long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
  printf("\nsquirted %d bytes in %0.02f seconds ", fileLength, ((double)micros)/1000000.0f);
  printFormatSpeed(fileLength, ((double)micros)/1000000.0f);
  printf("\n");

  cleanupAndExit();

  return 0;
}
