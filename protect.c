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

void
protect_cleanup(void)
{

}


int
protect_file(const char* filename, uint32_t protection, dir_datestamp_t* dateStamp)
{
  dir_datestamp_t _dateStamp = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  if (dateStamp == 0) {
    dateStamp = &_dateStamp;
  }

  if (util_sendCommand(main_socketFd, SQUIRT_COMMAND_SET_INFO) != 0) {
    fatalError("failed to connect to squirtd server");
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, filename) != 0) {
    fatalError("send() name failed");
  }

  if (util_sendU32(main_socketFd, protection) != 0) {
    fatalError("send() protection failed");
  }

  if (util_sendU32(main_socketFd, dateStamp->days) != 0) {
    fatalError("send() datestamp failed");
  }

  if (util_sendU32(main_socketFd, dateStamp->mins) != 0) {
    fatalError("send() datestamp failed");
  }

  if (util_sendU32(main_socketFd, dateStamp->ticks) != 0) {
    fatalError("send() datestamp failed");
  }

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("protect: failed to read remote status");
  }

  if (error != 0) {
    fprintf(stderr, "\n**FAILED** to protect %s\n%s\n", filename, util_getErrorString(error));
  }

  return error;
}
