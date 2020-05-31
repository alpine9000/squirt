#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <iconv.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

#ifndef _WIN32
#include <pwd.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include "squirt.h"
#include "common.h"

static const char* errors[] = {
  [_ERROR_SUCCESS] = "Unknown error",
  [ERROR_RECV_FAILED] = "recv failed",
  [ERROR_SEND_FAILED] = "send failed",
  [ERROR_CREATE_FILE_FAILED] = "create file failed",
  [ERROR_FILE_WRITE_FAILED] = "file write failed",
  [ERROR_FILE_READ_FAILED] = "file read failed",
  [ERROR_CD_FAILED] = "cd failed",
  [ERROR_FAILED_TO_CREATE_OS_RESOURCE] = "failed to create os resource",
  [ERROR_EXEC_FAILED] = "exec failed",
};

const char*
util_getHistoryFile(void)
{
  static char buffer[PATH_MAX];
  snprintf(buffer, sizeof(buffer), "%s/.squirt_history", util_getHomeDir());
  return buffer;
}

const char*
util_getHomeDir(void)
{
#ifndef _WIN32
  struct passwd *pw = getpwuid(getuid());
  return  pw->pw_dir;
#else
  return getenv("USERPROFILE");
#endif
}

static int
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


int
util_connect(const char* hostname, uint32_t commandCode)
{
  struct sockaddr_in sockAddr;
  int socketFd;

  commandCode = htonl(commandCode);

  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    return -1;
  }

  if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return -2;
  }

  if (connect(socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    return -3;
  }

  if (send(socketFd, (const void*)&commandCode, sizeof(commandCode), 0) != sizeof(commandCode)) {
    return -4;
  }

  return socketFd;
}


int
util_mkdir(const char *path, uint32_t mode)
{
#ifndef _WIN32
  return mkdir(path, mode);
#else
  (void)mode;
  DWORD dwAttrib = GetFileAttributes(path);

  if (!(dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))) {
    return !CreateDirectory(path, NULL);
  }

  return 0;
#endif
}


const char*
util_formatNumber(int number)
{
  static char buffer[256];
#ifdef _WIN32
  snprintf(buffer, sizeof(buffer), "%d", number);
#else
  snprintf(buffer, sizeof(buffer), "%'d", number);
#endif
  return buffer;
}


int
util_open(const char* filename, uint32_t mode)
{
#ifdef _WIN32
  return open(filename, mode|O_BINARY);
#else
  return open(filename, mode);
#endif
}


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

#ifndef _WIN32
  printf("\r%c[K", 27);
#else
  printf("\r");
#endif
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
#ifndef _WIN32
  fflush(stdout);
#endif
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
util_sendU32(int socketFd, uint32_t data)
{
  uint32_t networkData = htonl(data);

  if (send(socketFd, (const void*)&networkData, sizeof(networkData), 0) != sizeof(networkData)) {
    return -1;
  }

  return 0;
}


int
util_recvU32(int socketFd, uint32_t *data)
{
  if (util_recv(socketFd, data, sizeof(uint32_t), 0) != sizeof(uint32_t)) {
    return -1;
  }
  *data = ntohl(*data);
  return 0;
}


int
util_recv32(int socketFd, int32_t *data)
{
  if (util_recv(socketFd, data, sizeof(int32_t), 0) != sizeof(int32_t)) {
    return -1;
  }
  *data = ntohl(*data);
  return 0;
}


int
util_sendLengthAndUtf8StringAsLatin1(int socketFd, const char* str)
{
  int error = 0;
  char* latin1 = util_utf8ToLatin1(str);
  uint32_t length = strlen(latin1);
  uint32_t networkLength = htonl(length);

  if (send(socketFd, (const void*)&networkLength, sizeof(networkLength), 0) == sizeof(networkLength)) {
    error = send(socketFd, latin1, length, 0) != (int)length;
  }

  free(latin1);
  return error;
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

static void (*util_onCtrlChandler)(void) = 0;

#ifdef _WIN32
BOOL WINAPI
util_consoleHandler(DWORD signal)
{
  if (signal == CTRL_C_EVENT) {
    if (util_onCtrlChandler) {
      util_onCtrlChandler();
    }
  }
  return TRUE;
}
#else
void
util_signalHandler(int signal)
{
  if (signal == SIGINT) {
    if (util_onCtrlChandler) {
      util_onCtrlChandler();
    }
  }
}
#endif

void
util_onCtrlC(void (*handler)(void))
{
  util_onCtrlChandler = handler;
#ifdef _WIN32
  SetConsoleCtrlHandler(util_consoleHandler, TRUE);
#else
  signal(SIGINT, util_signalHandler);
#endif
}


#ifdef _WIN32
size_t
strlcat(char * restrict dst, const char * restrict src, size_t maxlen)
{
  const size_t srclen = strlen(src);
  const size_t dstlen = strnlen(dst, maxlen);
  if (dstlen == maxlen) return maxlen+srclen;
  if (srclen < maxlen-dstlen) {
    memcpy(dst+dstlen, src, srclen+1);
  } else {
    memcpy(dst+dstlen, src, maxlen-1);
    dst[dstlen+maxlen-1] = '\0';
  }
  return dstlen + srclen;
}
#endif
