#ifdef AMIGA
#include <proto/exec.h>
#include <proto/socket.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <stdint.h>
#include "squirtd.h"
#include "common.h"

uint32_t
exec_cd(const char* dir, int socketFd)
{
  uint32_t error = 0;
  printf("exec_cd: %s", dir);

  BPTR lock = Lock(dir, ACCESS_READ);

  if (!lock) {
    printf("failed to get lock!\n");
    error =  ERROR_CD_FAILED;
    goto cleanup;
  }

  BPTR oldLock = CurrentDir(lock);

  if (oldLock) {
    UnLock(oldLock);
  }

 cleanup:
  {
    char buffer = 0;
    send(socketFd, &buffer, 1, 0);
  }

  return error;
}

uint32_t
exec_run(const char* command, int socketFd)
{
  uint32_t error = 0;
  file_handle_t outputFd, inputFd = 0;

  if ((outputFd = Open("PIPE:alex", MODE_NEWFILE)) == 0) {
    error = ERROR_PIPE_OPEN_FAILED;
    goto cleanup;
  }

  struct TagItem tags[] = {
    { SYS_Asynch, 1},
    { SYS_Input, 0},
    { SYS_Output, (file_handle_t)outputFd},
    { TAG_DONE, 0}
  };

  if ((inputFd = Open("PIPE:alex", MODE_OLDFILE)) == 0) {
    error = ERROR_PIPE_OPEN_FAILED;
    goto cleanup;
  }

  if (SystemTagList(command, tags) != 0) {
    Close(outputFd);
    error = ERROR_EXEC_FAILED;
    goto cleanup;
  }

  char buffer[16];
  int length = sizeof(buffer);
  while ((length = Read(inputFd, buffer, length)) > 0) {
    send(socketFd, buffer, length, 0);
  }

 cleanup:

  buffer[0] = 0;
  send(socketFd, buffer, 1, 0);

  if (inputFd) {
    Close(inputFd);
  }

  return error;
}
#else // AMIGA

void
exec_cd(const char* dir)
{
  (void)dir;
}

void
exec_run(const char* command, int socketFd)
{
  (void)command;
  (void)socketFd;
}
#endif
