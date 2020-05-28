#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <iconv.h>
#include <stdlib.h>
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

int
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

  if (recv(socketFd, buffer, length, 0) != length) {
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
