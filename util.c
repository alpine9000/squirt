#ifdef linux
#define _XOPEN_SOURCE 500
#define _POSIX_C_SOURCE 200809L
#endif
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <iconv.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <ftw.h>
#include <pwd.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include "main.h"
#include "common.h"
#include "argv.h"

static const char* errors[] = {
  [_ERROR_SUCCESS] = "Unknown error",
  [ERROR_FATAL_ERROR] = "fatal error",
  [ERROR_FATAL_RECV_FAILED] = "recv failed",
  [ERROR_FATAL_SEND_FAILED] = "send failed",
  [ERROR_FATAL_FAILED_TO_CREATE_OS_RESOURCE] = "failed to create os resource",
  [ERROR_FATAL_CREATE_FILE_FAILED] = "create file failed",
  [ERROR_FATAL_FILE_WRITE_FAILED] = "file write failed",
  [ERROR_FILE_READ_FAILED] = "file read failed",
  [ERROR_SET_DATESTAMP_FAILED] = "set datestamp failed",
  [ERROR_SET_PROTECTION_FAILED] = "set protection failed",
  [ERROR_CD_FAILED] = "cd failed",
  [ERROR_EXEC_FAILED] = "exec failed",
  [ERROR_SUCK_ON_DIR] = "suck on dir",
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


void
util_connect(const char* hostname)
{
  struct sockaddr_in sockAddr;


  if (!util_getSockAddr(hostname, NETWORK_PORT, &sockAddr)) {
    goto error;
  }

  if ((main_socketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    goto error;
  }

  if (connect(main_socketFd, (struct sockaddr *)&sockAddr, sizeof (struct sockaddr_in)) < 0) {
    goto error;
  }

  return;
 error:
  fatalError("failed to connect to server %s", hostname);
}


int
util_mkpath(const char *dir)
{
  int error = 0;
  char tmp[PATH_MAX];
  char *p = NULL;
  size_t len;
  int makeLast = 0;

  snprintf(tmp, sizeof(tmp),"%s",dir);

  len = strlen(tmp);

  if (tmp[len - 1] == '/') {
    makeLast = 1;
    tmp[len - 1] = 0;
  }

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      util_mkdir(tmp, S_IRWXU);
      *p = '/';
    }
  }

  if (makeLast) {
    util_mkdir(tmp, S_IRWXU);
  }

  return error;
}


int
util_dirOperation(const char* directory, void (*operation)(const char* filename, void* data), void* data)
{
  DIR * dir =  opendir(directory);
  if (!dir) {
    return -1;
  }

  struct dirent *dp;
  while ((dp = readdir(dir)) != NULL) {
    if (operation) {
      operation(dp->d_name, data);
    }
  }

  closedir(dir);
  return 1;
}


#ifndef _WIN32
static int
_util_nftwRmFiles(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb)
{
  (void)sbuf,(void)type,(void)ftwb;

  return remove(pathname);
}


int
util_rmdir(const char *dir)
{
  int error = 0;

  if (nftw(dir, _util_nftwRmFiles,10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0) {
    error = -1;
  }

  return error;
}
#else
int
util_rmdir(const char* path)
{
  char* doubleTerminatedPath = malloc(strlen(path)+3);
  memset(doubleTerminatedPath, 0, strlen(path)+3);
  strcpy(doubleTerminatedPath, path);

  SHFILEOPSTRUCT fileOperation;
  fileOperation.wFunc = FO_DELETE;
  fileOperation.pFrom = doubleTerminatedPath;
  fileOperation.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION;

  return SHFileOperation(&fileOperation);

}
#endif


int
util_mkdir(const char *path, uint32_t mode)
{
#ifndef _WIN32
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    return 0;
  }

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


char*
util_latin1ToUtf8(const char* _buffer)
{
  if (_buffer) {
    iconv_t ic = iconv_open("UTF-8", "ISO-8859-1");
    char* buffer = malloc(strlen(_buffer)+1);
    if (!buffer) {
      return NULL;
    }
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

  return NULL;
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
util_printProgress(const char* filename, struct timeval* start, uint32_t total, uint32_t fileLength)
{
  (void)filename;
  int percentage;

  if (fileLength) {
    percentage = (((uint64_t)total*(uint64_t)100)/(uint64_t)fileLength);
  } else {
    percentage = 100;
  }

  int barWidth = main_screenWidth - 23;
  int screenPercentage = (percentage*barWidth)/100;
  struct timeval current;

#ifndef _WIN32
  printf("\r%c[K", 27);
#else
  printf("\r");
#endif
  fflush(stdout);
  if (percentage >= 100) {
    printf("\xE2\x9C\x85 "); // utf-8 tick
  } else {
    printf("\xE2\x8C\x9B "); // utf-8 hourglass
  }

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


size_t
util_strlcat(char * restrict dst, const char * restrict src, size_t maxlen)
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

const char*
util_getTempFolder(void)
{
  static char path[PATH_MAX];
#ifndef _WIN32
  snprintf(path, sizeof(path), "/tmp/.squirt/%d/", getpid());
  return path;
#else
  char buffer[PATH_MAX];
  GetTempPathA(PATH_MAX, buffer);
  snprintf(path, sizeof(path), "%s.squirt/%d/", buffer, getpid());
  return path;
#endif
}


int
util_isDirectory(const char *path)
{
#ifdef _WIN32
  struct _stat statbuf;
  if (_stat(path, &statbuf) != 0)
    return 0;
  return statbuf.st_mode & _S_IFDIR;
#else
  struct stat statbuf;
  if (stat(path, &statbuf) != 0)
    return 0;
  return S_ISDIR(statbuf.st_mode);
#endif

}


int
util_exec(char* command)
{
  char** argv = argv_build(command);
  int error = exec_cmd(argv_argc(argv), argv);
  argv_free(argv);
  return error;
}


char*
util_execCapture(char* command)
{
  char** argv = argv_build(command);
  uint32_t error = 0;
  char* output = exec_captureCmd(&error, argv_argc(argv), argv);
  argv_free(argv);
  if (error) {
    if (output) {
      free(output);
    }
    return 0;
  }
  return output;
}


int
util_system(char** argv)
{
  int argc = argv_argc(argv);
  int commandLength = 0;
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      commandLength++;
    }
    commandLength += (strlen(argv[i])+2);
  }
  commandLength++;

  char* command = malloc(commandLength);
  command[0] = 0;
  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      strcat(command, " ");
    }
    strcat(command, argv[i]);
  }

  int error = system(command);
  free(command);
  return error;
}


int
util_cd(const char* dir)
{
  uint32_t error = 0;

  if (util_sendCommand(main_socketFd, SQUIRT_COMMAND_CD) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, dir) != 0) {
    fatalError("send() command failed");
  }

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("cd: failed to read remote status");
  }

  return error;
}


char*
util_safeName(const char* name)
{
  char* safe = malloc(strlen(name)+1);
  char* dest = safe;
  if (dest) {
    while (*name) {
      if (*name != ':') {
	*dest = *name;
	dest++;
      }
      name++;
    }
    *dest = 0;
  }

  return safe;
}
