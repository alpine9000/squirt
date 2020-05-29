#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <iconv.h>
#include <stdlib.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "squirt.h"
#include "common.h"

static const char* errors[] = {
  [ERROR_SUCCESS] = "Unknown error",
  [ERROR_RECV_FAILED] = "recv failed",
  [ERROR_SEND_FAILED] = "send failed",
  [ERROR_CREATE_FILE_FAILED] = "create file failed",
  [ERROR_FILE_WRITE_FAILED] = "file write failed",
  [ERROR_FILE_READ_FAILED] = "file read failed",
  [ERROR_CD_FAILED] = "cd failed",
  [ERROR_FAILED_TO_CREATE_OS_RESOURCE] = "failed to create os resource",
  [ERROR_EXEC_FAILED] = "exec failed",
};

static char*
util_utf8ToLatin1(const char* buffer)
{
  iconv_t ic = iconv_open("ISO-8859-1", "UTF-8");
  size_t insize = strlen(buffer);
  char* inptr = (char*)buffer;
  size_t outsize = (insize)+1;
  char* out = calloc(1, outsize);
  char* outptr = out;
  iconv(ic, &inptr, &insize, &outptr, &outsize);
  iconv_close(ic);
  return out;
}


static char*
util_latin1ToUtf8(const char* _buffer)
{
  iconv_t ic = iconv_open("UTF-8", "ISO-8859-1");
  char* buffer = malloc(strlen(_buffer)+1);
  strcpy(buffer, _buffer);
  size_t insize = strlen(buffer);
  char* inptr = (char*)buffer;
  size_t outsize = (insize*4)+1;
  char* out = calloc(1, outsize);
  char* outptr = out;
  iconv(ic, &inptr, &insize, &outptr, &outsize);
  iconv_close(ic);
  free(buffer);
  return out;
}


void
util_printFormatSpeed(int32_t size, double elapsed)
{
  double speed = (double)size/elapsed;
  if (speed < 1000) {
    printf("%0.2f b/s", speed);
  } else if (speed < 1000000) {
    printf("%0.2f kB/s", speed/1000.0f);
  } else {
    printf("%0.2f MB/s", speed/1000000.0f);
  }
}

void
util_printProgress(struct timeval* start, uint32_t total, uint32_t fileLength)
{
  int percentage;

  if (fileLength) {
    percentage = (total*100)/fileLength;
  } else {
    percentage = 100;
  }
  int barWidth = squirt_screenWidth - 20;
  int screenPercentage = (percentage*barWidth)/100;
  struct timeval current;

  printf("\r%c[K", 27);
  printf("%3d%% [", percentage);

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
  util_printFormatSpeed(total, ((double)micros)/1000000.0f);
  fflush(stdout);
}



const char*
util_amigaBaseName(const char* filename)
{
  int i;
  for (i = strlen(filename)-1; i > 0 && filename[i] != '/' && filename[i] != ':'; --i);
  if (i > 0) {
    filename = &filename[i+1];
  }
  return filename;
}

size_t
util_recv(int socket, void *buffer, size_t length, int flags)
{
  uint32_t total = 0;
  char* ptr = buffer;
  do {
    int got = recv(socket, ptr, length-total, flags);
    if (got > 0) {
      total += got;
      ptr += got;
    } else {
      return got;
    }
  } while (total < length);

  return total;
}

int
util_recvU32(int socketFd, uint32_t *data)
{
  if (util_recv(socketFd, data, sizeof(uint32_t), 0) != sizeof(uint32_t)) {
    return 0;
  }
  *data = ntohl(*data);
  return 1;
}

int
util_sendLengthAndUtf8StringAsLatin1(int socketFd, const char* str)
{
  int success = 1;
  char* latin1 = util_utf8ToLatin1(str);
  uint32_t length = strlen(latin1);
  uint32_t networkLength = htonl(length);

  if (send(socketFd, &networkLength, sizeof(networkLength), 0) == sizeof(networkLength)) {
    success = send(socketFd, latin1, length, 0) == length;
  }

  free(latin1);
  return success;
}

char*
util_recvLatin1AsUtf8(int socketFd, uint32_t length)
{
  char* buffer = malloc(length+1);

  if (util_recv(socketFd, buffer, length, 0) != length) {
    free(buffer);
    return 0;
  }

  buffer[length] = 0;

  char* utf8 = util_latin1ToUtf8(buffer);
  free(buffer);
  return utf8;
}

const char*
util_getErrorString(uint32_t error)
{
  if (error >= sizeof(errors)) {
    error = 0;
  }

  return errors[error];
}


int
util_getSockAddr(const char * host, int port, struct sockaddr_in * addr)
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
