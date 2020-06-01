#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <proto/dos.h>
#include "common.h"

//#define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
#include <stdio.h>
#define fatalError(x) _fatalError(x)
#else
#define printf(...)
#define fprintf(...)
#define fatalError(x) _fatalError()
#endif

static uint32_t squirtd_error = 0;
static int squirtd_listenFd = 0;
static int squirtd_connectionFd = 0;
static char* squirtd_filename = 0;
static char* squirtd_rxBuffer = 0;
static BPTR  squirtd_outputFd = 0;
static BPTR squirtd_inputFd = 0;

static const char* exec_command;
static BPTR exec_inputFd, exec_outputFd;

#ifdef __GNUC__
struct Library *SocketBase = 0;

static void _cleanup(void)
{
  if (SocketBase) {
    CloseLibrary(SocketBase);
  }
}
#endif


static void
cleanupForNextRun(void)
{
  if (squirtd_inputFd > 0) {
    Close(squirtd_inputFd);
    squirtd_inputFd = 0;
  }

  if (squirtd_connectionFd > 0) {
    CloseSocket(squirtd_connectionFd);
    squirtd_connectionFd = 0;
  }

  if (squirtd_rxBuffer) {
    free(squirtd_rxBuffer);
    squirtd_rxBuffer = 0;
  }

  if (squirtd_filename) {
    free(squirtd_filename);
    squirtd_filename = 0;
  }

  if (squirtd_outputFd > 0) {
    Close(squirtd_outputFd);
    squirtd_outputFd = 0;
  }
}


static void
cleanup(void)
{
  if (squirtd_listenFd > 0) {
    CloseSocket(squirtd_listenFd);
    squirtd_listenFd = 0;
  }

  cleanupForNextRun();
}


static void
#ifdef DEBUG_OUTPUT
_fatalError(char* msg)
#else
_fatalError(void)
#endif
{
  fprintf(stderr, msg);
  cleanup();
  exit(1);
}


static void
sendStatus(int fd, uint32_t status)
{
  send(fd, (void*)&status, sizeof(status), 0);
}


static void
exec_runner(void)
{
  squirtd_error = SystemTags((APTR)exec_command, SYS_Output, exec_outputFd, TAG_DONE, 0) == 0 ? 0 : ERROR_EXEC_FAILED;

  if (exec_outputFd) {
    Close(exec_outputFd);
  }
}


static void
exec_run(int fd, const char* command)
{
  exec_command = command;
  const char* pipe = "PIPE:";

  if ((exec_outputFd = Open((APTR)pipe, MODE_NEWFILE)) == 0) {
    squirtd_error = ERROR_FAILED_TO_CREATE_OS_RESOURCE;
    goto cleanup;
  }

  if ((exec_inputFd = Open((APTR)pipe, MODE_OLDFILE)) == 0) {
    squirtd_error = ERROR_FAILED_TO_CREATE_OS_RESOURCE;
    goto cleanup;
  }

  CreateNewProcTags(NP_Entry, (uint32_t)exec_runner, TAG_DONE, 0);

  char buffer[16];
  int length;
  while ((length = Read(exec_inputFd, buffer, sizeof(buffer))) > 0) {
    send(fd, buffer, length, 0);
  }

 cleanup:

  // not exit status, sending 4 null bytes breaks out of the terminal read loop in squirt_execCmd
  // reusing sendStatus for executable size

  sendStatus(fd, 0);

  if (exec_inputFd) {
    Close(exec_inputFd);
  }
}


static void
exec_dir(int fd, const char* dir)
{
  struct ExAllControl*  eac = 0;
  void* data = 0;

  BPTR lock = Lock((APTR)dir, ACCESS_READ);

  squirtd_error = 0;

  if (!lock) {
    squirtd_error = ERROR_FILE_READ_FAILED;
    uint32_t nameLength = 0;
    send(fd, (void*)&nameLength, sizeof(nameLength), 0);
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
    more = ExAll(lock, data, BLOCK_SIZE, ED_COMMENT, eac);
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
      uint32_t nameLength = strlen((char*)ead->ed_Name);
      uint32_t commentLength = strlen((char*)ead->ed_Comment);
      if (send(fd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength) ||
	  send(fd, ead->ed_Name, nameLength, 0) != (int)nameLength ||
	  send(fd, (void*)&ead->ed_Type, sizeof(ead->ed_Type), 0) != sizeof(ead->ed_Type) ||
	  send(fd, (void*)&ead->ed_Size, sizeof(ead->ed_Size), 0) != sizeof(ead->ed_Size) ||
	  send(fd, (void*)&ead->ed_Prot, sizeof(ead->ed_Prot), 0) != sizeof(ead->ed_Prot) ||
	  send(fd, (void*)&ead->ed_Days, sizeof(ead->ed_Days), 0) != sizeof(ead->ed_Days) ||
	  send(fd, (void*)&ead->ed_Mins, sizeof(ead->ed_Mins), 0) != sizeof(ead->ed_Mins) ||
	  send(fd, (void*)&ead->ed_Ticks, sizeof(ead->ed_Ticks), 0) != sizeof(ead->ed_Ticks) ||
	  send(fd, (void*)&commentLength, sizeof(commentLength), 0) != sizeof(commentLength) ||
	  send(fd, ead->ed_Comment, commentLength, 0) != (int)commentLength) {
	squirtd_error = ERROR_SEND_FAILED;
	goto cleanup;
      }
      ead = ead->ed_Next;
    } while (ead);
  } while (more);


 cleanup:

  sendStatus(fd, squirtd_error);

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


static uint32_t
exec_cwd(int fd)
{
  char name[108];
  struct Process *me = (struct Process*)FindTask(0);
  NameFromLock(me->pr_CurrentDir, (STRPTR)name, sizeof(name)-1);
  int32_t len = strlen(name);

  if (send(fd, (void*)&len, sizeof(len), 0) != sizeof(len)) {
    return ERROR_SEND_FAILED;
  }
  if (send(fd, name, len, 0) != len) {
    return ERROR_SEND_FAILED;
  }

  return _ERROR_SUCCESS;
}


static void
exec_cd(int fd, const char* dir)
{
  BPTR lock = Lock((APTR)dir, ACCESS_READ);
  squirtd_error = ERROR_CD_FAILED;

  if (!lock) {
    goto cleanup;
  }

  struct FileInfoBlock fileInfo;
  Examine(lock, &fileInfo);

  if (fileInfo.fib_DirEntryType > 0) {
    BPTR oldLock = CurrentDir(lock);

    if (oldLock) {
      UnLock(oldLock);
      squirtd_error = 0;
    }
  } else {
    UnLock(lock);
  }

 cleanup:
  sendStatus(fd, squirtd_error);
}


static int16_t
file_get(int fd, const char* destFolder, uint32_t nameLength, uint32_t writeToCwd)
{
  int destFolderLen = strlen(destFolder);
  int fullPathLen = nameLength+destFolderLen;
  squirtd_filename = malloc(fullPathLen+1);
  char* filenamePtr;

  if (writeToCwd) {
    filenamePtr = squirtd_filename;
    fullPathLen = nameLength;
  }  else {
    strcpy(squirtd_filename, destFolder);
    filenamePtr = squirtd_filename+destFolderLen;
  }

  if (recv(fd, filenamePtr, nameLength, 0) != (int)nameLength) {
    return ERROR_RECV_FAILED;
  }

  squirtd_filename[fullPathLen] = 0;

  int32_t fileLength;
  if (recv(fd, (void*)&fileLength, sizeof(fileLength), 0) != sizeof(fileLength)) {
    return  ERROR_RECV_FAILED;
  }

  DeleteFile((APTR)squirtd_filename);

  if ((squirtd_outputFd = Open((APTR)squirtd_filename, MODE_NEWFILE)) == 0) {
    return ERROR_CREATE_FILE_FAILED;
  }

  squirtd_rxBuffer = malloc(BLOCK_SIZE);
  int total = 0, timeout = 0, length;
  do {
    if ((length = recv(fd, (void*)squirtd_rxBuffer, BLOCK_SIZE, 0)) < 0) {
      return ERROR_RECV_FAILED;
    }
    if (length) {
      total += length;
      if (Write(squirtd_outputFd, squirtd_rxBuffer, length) != length) {
	return ERROR_FILE_WRITE_FAILED;
      }
      timeout = 0;
    } else {
      timeout++;
    }
  } while (timeout < 2 && total < fileLength);

  printf("got %s -> %d\n", squirtd_filename, total);

  return 0;
}


static int16_t
file_send(int fd, char* filename)
{
  int32_t fileSize = -1;
  BPTR lock = Lock((APTR)filename, ACCESS_READ);
  if (!lock) {
    if (send(fd, (void*)&fileSize, sizeof(fileSize), 0) != sizeof(fileSize)) {
      return ERROR_SEND_FAILED;
    }
    return ERROR_FILE_READ_FAILED;
  }

  struct FileInfoBlock fileInfo;
  Examine(lock, &fileInfo);
  UnLock(lock);

  fileSize = fileInfo.fib_Size;

  if (send(fd, (void*)&fileSize, sizeof(fileSize), 0) != sizeof(fileSize)) {
    return ERROR_SEND_FAILED;
  }

  if (fileSize == 0) {
    return 0;
  }

  squirtd_inputFd = Open((APTR)squirtd_filename, MODE_OLDFILE);

  if (!squirtd_inputFd) {
    printf("open failed %s\n", filename);
    return ERROR_FILE_READ_FAILED;
  }

  squirtd_rxBuffer = malloc(BLOCK_SIZE);

  int32_t total = 0;
  do {
    int len;
    if ((len = Read(squirtd_inputFd, squirtd_rxBuffer, BLOCK_SIZE) ) < 0) {
      return ERROR_FILE_READ_FAILED;
    } else {
      if (send(fd, squirtd_rxBuffer, len, 0) != len) {
	return ERROR_SEND_FAILED;
      }
      total += len;
    }

  } while (total < fileSize);

  return 0;
}


int
main(int argc, char **argv)
{
  if (argc != 2) {
    fatalError("squirtd: dest_folder\n");
  }

  struct Process *me = (struct Process*)FindTask(0);
  me->pr_WindowPtr = (APTR)-1; // disable requesters

#ifdef __GNUC__
  atexit(_cleanup);
  SocketBase = OpenLibrary((APTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    fatalError("failed to open bsdsocket.library");
  }
#endif

  struct sockaddr_in sa = {0};
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = 0; //inet_addr("0.0.0.0");
  sa.sin_port = htons(NETWORK_PORT);

  if ((squirtd_listenFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    fatalError("socket() failed\n");
  }

  const int ONE = 1;
  setsockopt(squirtd_listenFd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(squirtd_listenFd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    fatalError("bind() failed\n");
  }

  if (listen(squirtd_listenFd, 1)) {
    fatalError("listen() failed\n");
  }


 again:
  printf("restarting\n");

  squirtd_error = 0;

  if ((squirtd_connectionFd = accept(squirtd_listenFd, 0, 0)) == -1) {
    fatalError("accept() failed\n");
  }

  LONG socketTimeout = 1000;
  setsockopt(squirtd_connectionFd, SOL_SOCKET, SO_RCVTIMEO, (char*)&socketTimeout, sizeof(socketTimeout));

  uint32_t command;
  if (recv(squirtd_connectionFd, (void*)&command, sizeof(command), 0) != sizeof(command)) {
    squirtd_error = ERROR_RECV_FAILED;
    goto cleanup;
  }

  uint32_t nameLength;
  if (recv(squirtd_connectionFd, (void*)&nameLength, sizeof(nameLength), 0) != sizeof(nameLength)) {
    squirtd_error = ERROR_RECV_FAILED;
    goto cleanup;
  }

  if (command > SQUIRT_COMMAND_SQUIRT_TO_CWD) {
    squirtd_filename = malloc(nameLength+1);

    if (recv(squirtd_connectionFd, squirtd_filename, nameLength, 0) != (int)nameLength) {
      squirtd_error = ERROR_RECV_FAILED;
      goto cleanup;
    }

    squirtd_filename[nameLength] = 0;

    if (command == SQUIRT_COMMAND_CLI) {
      exec_run(squirtd_connectionFd, squirtd_filename);
    } else if (command == SQUIRT_COMMAND_CD) {
      exec_cd(squirtd_connectionFd, squirtd_filename);
    } else if (command == SQUIRT_COMMAND_SUCK) {
      file_send(squirtd_connectionFd, squirtd_filename);
    } else if (command == SQUIRT_COMMAND_DIR) {
      exec_dir(squirtd_connectionFd, squirtd_filename);
    } else if (command == SQUIRT_COMMAND_CWD) {
      exec_cwd(squirtd_connectionFd);
    }
    goto cleanup;
  } else {
    squirtd_error = file_get(squirtd_connectionFd, argv[1], nameLength, command == SQUIRT_COMMAND_SQUIRT_TO_CWD);
    if (squirtd_error) {
      goto cleanup;
    }
  }

 cleanup:
  sendStatus(squirtd_connectionFd, squirtd_error);
  cleanupForNextRun();

  goto again;

  return 0;
}
