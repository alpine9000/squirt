#ifdef AMIGA
#include <proto/exec.h>
#include <proto/socket.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/exall.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "squirtd.h"
#include "common.h"

static const char* exec_command;
static file_handle_t exec_inputFd, exec_outputFd;

static void
exec_runner(void)
{
  squirtd_error = SystemTags(exec_command, SYS_Output, exec_outputFd, TAG_DONE, 0) == 0 ? 0 : ERROR_EXEC_FAILED;

  if (exec_outputFd) {
    Close(exec_outputFd);
  }
}


void
exec_run(const char* command, int socketFd)
{
  exec_command = command;
  const char* pipe = "PIPE:";

  if ((exec_outputFd = Open(pipe, MODE_NEWFILE)) == 0) {
    squirtd_error = ERROR_FAILED_TO_CREATE_OS_RESOURCE;
    goto cleanup;
  }

  if ((exec_inputFd = Open(pipe, MODE_OLDFILE)) == 0) {
    squirtd_error = ERROR_FAILED_TO_CREATE_OS_RESOURCE;
    goto cleanup;
  }

  CreateNewProcTags(NP_Entry, exec_runner, TAG_DONE, 0);

  char buffer[16];
  int length = sizeof(buffer);
  while ((length = Read(exec_inputFd, buffer, length)) > 0) {
    send(socketFd, buffer, length, 0);
  }

 cleanup:

  buffer[0] = 0;
  send(socketFd, buffer, 1, 0);

  if (exec_inputFd) {
    Close(exec_inputFd);
  }
}

void
exec_dir(const char* dir, int socketFd)
{
  struct ExAllControl*  eac = 0;
  void* data = 0;

  BPTR lock = Lock(dir, ACCESS_READ);

  if (!lock) {
    squirtd_error = ERROR_FILE_READ_FAILED;
    goto cleanup;
  }

  data = malloc(BLOCK_SIZE);

  eac = AllocDosObject(DOS_EXALLCONTROL,NULL);

  if (!eac) {
    squirtd_error = ERROR_FAILED_TO_CREATE_OS_RESOURCE;
    goto cleanup;
  }

  eac->eac_LastKey = 0;
  int more;
  do {
    more = ExAll(lock, data, BLOCK_SIZE, ED_DATE, eac);
    if ((!more) && (IoErr() != ERROR_NO_MORE_ENTRIES)) {
      goto cleanup;
      break;
    }
    if (eac->eac_Entries == 0) {
      /* ExAll failed normally with no entries */
      continue; /* ("more" is *usually* zero) */
    }
    struct ExAllData *ead = (struct ExAllData *) data;
    do {
      printf("%s: ead->ed_Prot = %x\n", ead->ed_Name, ead->ed_Prot);
      uint32_t nameLength = strlen(ead->ed_Name);
      if (send(socketFd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength) ||
	  send(socketFd, ead->ed_Name, nameLength, 0) != nameLength ||
	  send(socketFd, (void*)&ead->ed_Type, sizeof(ead->ed_Type), 0) != sizeof(ead->ed_Type) ||
	  send(socketFd, (void*)&ead->ed_Size, sizeof(ead->ed_Size), 0) != sizeof(ead->ed_Size) ||
	  send(socketFd, (void*)&ead->ed_Prot, sizeof(ead->ed_Prot), 0) != sizeof(ead->ed_Prot) ||
	  send(socketFd, (void*)&ead->ed_Days, sizeof(ead->ed_Days), 0) != sizeof(ead->ed_Days) ||
	  send(socketFd, (void*)&ead->ed_Mins, sizeof(ead->ed_Mins), 0) != sizeof(ead->ed_Mins) ||
	  send(socketFd, (void*)&ead->ed_Ticks, sizeof(ead->ed_Ticks), 0) != sizeof(ead->ed_Ticks)) {
	squirtd_error = ERROR_SEND_FAILED;
	goto cleanup;
      }
      ead = ead->ed_Next;
    } while (ead);
  } while (more);

  uint32_t buffer = 0;
  send(socketFd, (void*)&buffer, sizeof(buffer), 0);

 cleanup:

  if (eac) {
    FreeDosObject(DOS_EXALLCONTROL,eac);
  }

  if (data) {
    free(data);
  }

  if (lock) {
    UnLock(lock);
  }
}


void
exec_cd(const char* dir, int socketFd)
{
  BPTR lock = Lock(dir, ACCESS_READ);

  if (!lock) {
    squirtd_error = ERROR_CD_FAILED;
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

void
exec_dir(const char* dir, int socketFd)
{
  (void)dir;
  (void)socketFd;
}
#endif
