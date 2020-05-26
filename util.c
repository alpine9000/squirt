#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "squirt.h"
#include "common.h"

static const char* errors[] = {
  [ERROR_SUCCESS] = "Unknown error",
  [ERROR_RECV_FAILED_READING_COMMAND] = "recv() failed reading command",
  [ERROR_RECV_FAILED_READING_NAME_LENGTH] = "recv() failed reading name length",
  [ERROR_RECV_FAILED_READING_FILENAME] = "recv() failed reading filename",
  [ERROR_RECV_FAILED_READING_FILE_LENGTH] = "recv() failed reading file length",
  [ERROR_FAILED_TO_CREATE_DESTINATION_FILE] = "Failed to create destination file",
  [ERROR_RECV_FAILED_READING_FILE_DATA] = "recv() failed reading file data",
  [ERROR_WRITE_FAILED_WRITING_FILE_DATA] = "Write() failed writing file data",
  [ERROR_FAILED_TO_READ_FILE] = "failed to read file",
  [ERROR_SEND_FAILED_WRITING_FILENAME_LENGTH] = "send failed writing filename length",
  [ERROR_SEND_FAILED_WRITING_FILENAME] = "send failed writing filename",
  [ERROR_SEND_FAILED_WRITING_DATA] = "send failed writing data",
  [ERROR_SEND_FAILED_WRITING_SIZE] = "send failed writing size",
  [ERROR_CD_FAILED] = "cd failed",
  [ERROR_PIPE_OPEN_FAILED] = "pipe open failed",
  [ERROR_EXEC_FAILED] = "failed to create system shell",
};

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
