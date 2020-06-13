#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dostags.h>
#include <exec/execbase.h>
#include <proto/dos.h>
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

typedef struct {
  uint32_t protection;
  struct DateStamp dateStamp;
} squirtd_file_info_t;

static uint32_t squirtd_execError = 0;
static int squirtd_listenFd = 0;
static int squirtd_connectionFd = 0;
static char* squirtd_filename = 0;
static char* squirtd_rxBuffer = 0;
static BPTR squirtd_outputFd = 0;
static BPTR squirtd_inputFd = 0;


typedef struct {
  BPTR read;
  BPTR write;
} squirtd_pipe_fd_t;

static squirtd_pipe_fd_t exec_cmdPipeFd;
static squirtd_pipe_fd_t exec_bridgePipeFd;
static squirtd_pipe_fd_t exec_cmdInputFd;
static struct Task* exec_mainTask = 0;
static LONG exec_readSignal = 0;
static LONG exec_done = 0;
volatile int exec_cmdTxLength = 0;
static const char* exec_command;

#ifdef __GNUC__
struct Library *SocketBase = 0;
#endif


static void
exec_closeFd(BPTR* fd)
{
  if (*fd > 0) {
    Close(*fd);
    *fd = 0;
  }
}


static void
cleanupForNextRun(void)
{
  exec_done = 0;

  if (exec_readSignal) {
    FreeSignal(exec_readSignal);
    exec_readSignal = 0;
  }

  exec_closeFd(&squirtd_inputFd);
  exec_closeFd(&squirtd_outputFd);

  if (squirtd_rxBuffer) {
    free(squirtd_rxBuffer);
    squirtd_rxBuffer = 0;
  }

  if (squirtd_filename) {
    free(squirtd_filename);
    squirtd_filename = 0;
  }
}


static void
cleanup(void)
{
  if (squirtd_connectionFd > 0) {
    CloseSocket(squirtd_connectionFd);
    squirtd_connectionFd = 0;
  }

  if (squirtd_listenFd > 0) {
    CloseSocket(squirtd_listenFd);
    squirtd_listenFd = 0;
  }

  cleanupForNextRun();

#ifdef __GNUC__
  if (SocketBase) {
    CloseLibrary(SocketBase);
  }
#endif
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

static uint32_t
sendU32(int fd, uint32_t status)
{
  if (send(fd, (void*)&status, sizeof(status), 0) != sizeof(status)) {
    return ERROR_FATAL_SEND_FAILED;
  }

  return 0;
}


static void
exec_runner(void)
{
  squirtd_execError = SystemTags((APTR)exec_command, SYS_Input, exec_cmdInputFd.read, SYS_Output, exec_cmdPipeFd.write, TAG_DONE, 0) == 0 ? 0 : ERROR_EXEC_FAILED;

  exec_closeFd(&exec_cmdPipeFd.write);
  exec_closeFd(&exec_cmdInputFd.read);
}

static void
exec_pipeBridge(void)
{
  char buffer[1];
  int length;

  while ((length = Read(exec_cmdPipeFd.read, buffer, sizeof(buffer))) > 0) {
    if (Write(exec_bridgePipeFd.write, buffer, length) != length) {
      goto done;
    }
    exec_cmdTxLength += length;
    Signal(exec_mainTask, exec_readSignal);
  }

 done:

  exec_done = 1;

  exec_closeFd(&exec_bridgePipeFd.write);
}

static void
exec_sigIntCmd(void)
{
  Forbid();
  struct Process* me = (void*)FindTask(0);
  for (unsigned long i = 0; i < MaxCli(); i++) {
    struct Process * proc =  FindCliProc(i);
    if (proc != me && proc->pr_ConsoleTask == me->pr_ConsoleTask) {
      if (strcmp("Background CLI", proc->pr_Task.tc_Node.ln_Name) == 0) {
	Signal((struct Task*)proc, SIGBREAKF_CTRL_C);
      }
    }
  }
  Permit();
}


static uint32_t
exec_createPipe(squirtd_pipe_fd_t* fd, const char* name)
{
  if ((fd->write = Open((APTR)name, MODE_NEWFILE)) == 0) {
    return ERROR_FATAL_FAILED_TO_CREATE_OS_RESOURCE;
  }

  if ((fd->read = Open((APTR)name, MODE_OLDFILE)) == 0) {
    return ERROR_FATAL_FAILED_TO_CREATE_OS_RESOURCE;
  }

  return 0;
}

static uint32_t
exec_run(int fd, const char* command)
{
  exec_command = command;
  uint32_t error = 0;

  exec_cmdTxLength = 0;

  exec_mainTask = FindTask(NULL);
  exec_readSignal = AllocSignal(-1);
  SetSignal(0, exec_readSignal);

  if ((error = exec_createPipe(&exec_cmdPipeFd, "PIPE:0")) ||
      (error = exec_createPipe(&exec_cmdInputFd, "PIPE:1")) ||
      (error = exec_createPipe(&exec_bridgePipeFd, "PIPE:2"))) {
    goto cleanup;
  }

  CreateNewProcTags(NP_Name, (LONG)"EXEC RUNNER", NP_Entry, (uint32_t)exec_runner, TAG_DONE, 0);
  CreateNewProcTags(NP_Name, (LONG)"PIPE BRIDGE", NP_Entry, (uint32_t)exec_pipeBridge, TAG_DONE, 0);

  char buffer[1];
  int length;
  int done = 0;
  fd_set readFds = {0};
  ULONG signals = exec_readSignal;

  int rx = 0;

  while (!done) {
    FD_ZERO(&readFds);
    FD_SET(fd,&readFds);
    WaitSelect(fd+1, &readFds, 0, 0, 0, &signals);

    if (FD_ISSET(fd, &readFds)) {
      uint8_t c;
      recv(fd, &c, 1, 0);
      exec_sigIntCmd();
      done = 1;
    }

    if (SetSignal(0, exec_readSignal) & exec_readSignal) {
      while (rx < exec_cmdTxLength) {
	done = (length = Read(exec_bridgePipeFd.read, buffer, sizeof(buffer))) <= 0;
	rx += length;
	if (send(fd, buffer, length, 0) != length) {
	  error = ERROR_FATAL_SEND_FAILED;
	  goto cleanup;
	}
      }
    }

    if (exec_done) {
      done = 1;
      while ((length = Read(exec_bridgePipeFd.read, buffer, sizeof(buffer))) > 0) {
	if (send(fd, buffer, length, 0) != length) {
	  error = ERROR_FATAL_SEND_FAILED;
	  goto cleanup;
	}
      }
    }
  }

 cleanup:


  if (!error) {
    error = squirtd_execError;
  }

  // sending 4 null bytes breaks out of the terminal read loop in squirt_execCmd
  if (sendU32(fd, 0) != 0) {
    error = ERROR_FATAL_SEND_FAILED;
  }

  exec_closeFd(&exec_bridgePipeFd.read);
  exec_closeFd(&exec_cmdPipeFd.read);
  exec_closeFd(&exec_cmdInputFd.write);

  return error;
}


static uint32_t
exec_dir(int fd, const char* dir)
{
  struct ExAllControl*  eac = 0;
  void* data = 0;
  uint32_t error = 0;

  BPTR lock = Lock((APTR)dir, ACCESS_READ);

  if (!lock) {
    error = ERROR_FILE_READ_FAILED;
    goto cleanup;
  }

  data = malloc(BLOCK_SIZE);

  eac = AllocDosObject(DOS_EXALLCONTROL, NULL);

  if (!eac) {
    error = ERROR_FATAL_FAILED_TO_CREATE_OS_RESOURCE;
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
	error = ERROR_FATAL_SEND_FAILED;
	goto cleanup;
      }
      ead = ead->ed_Next;
    } while (ead);
  } while (more);


 cleanup:

  if (sendU32(fd, 0xFFFFFFFF) != 0) { ; // not status, terminating word
    error = ERROR_FATAL_SEND_FAILED;
  }

  if (eac) {
    FreeDosObject(DOS_EXALLCONTROL,eac);
  }

  if (data) {
    free(data);
  }

  if (lock) {
    UnLock(lock);
  }

  return error;
}


static uint32_t
exec_cwd(int fd)
{
  char name[108];
  struct Process *me = (struct Process*)FindTask(0);
  NameFromLock(me->pr_CurrentDir, (STRPTR)name, sizeof(name)-1);
  int32_t len = strlen(name);

  if (send(fd, (void*)&len, sizeof(len), 0) != sizeof(len) ||
      (send(fd, name, len, 0) != len)) {
    return ERROR_FATAL_SEND_FAILED;
  }

  return 0;
}


static uint32_t
exec_cd(const char* dir)
{
  BPTR lock = Lock((APTR)dir, ACCESS_READ);

  if (!lock) {
    return ERROR_CD_FAILED;
  }

  struct FileInfoBlock fileInfo;
  Examine(lock, &fileInfo);

  if (fileInfo.fib_DirEntryType > 0) {
    BPTR oldLock = CurrentDir(lock);

    if (oldLock) {
      UnLock(oldLock);
    }
    return 0;
  } else {
    UnLock(lock);
    return ERROR_CD_FAILED;
  }
}


static uint32_t
file_setInfo(int fd, const char* filename)
{
  squirtd_file_info_t info;
  int len;
  if ((len = recv(fd, &info, sizeof(info), 0)) == sizeof(info)) {
    if (!SetProtection((STRPTR)filename, info.protection)) {
      return ERROR_SET_PROTECTION_FAILED;
    }
    if ((uint32_t)info.dateStamp.ds_Days != 0xFFFFFFFF) {
      if (!SetFileDate((STRPTR)filename, &info.dateStamp)) {
	return ERROR_SET_DATESTAMP_FAILED;
      }
    }
    return 0;
  }

  return ERROR_FATAL_RECV_FAILED;
}


static uint32_t
file_get(int fd)
{
  int32_t fileLength;
  if (recv(fd, (void*)&fileLength, sizeof(fileLength), 0) != sizeof(fileLength)) {
    return ERROR_FATAL_RECV_FAILED;
  }

  DeleteFile((APTR)squirtd_filename);

  if ((squirtd_outputFd = Open((APTR)squirtd_filename, MODE_NEWFILE)) == 0) {
    return ERROR_FATAL_CREATE_FILE_FAILED;
  }

  squirtd_rxBuffer = malloc(BLOCK_SIZE);
  int total = 0, timeout = 0, length, blockSize = BLOCK_SIZE;
  do {
    if (fileLength-total < BLOCK_SIZE) {
      blockSize = fileLength-total;
    }
    if ((length = recv(fd, (void*)squirtd_rxBuffer, blockSize, 0)) < 0) {
      return ERROR_FATAL_RECV_FAILED;
    }
    if (length) {
      total += length;
      if (Write(squirtd_outputFd, squirtd_rxBuffer, length) != length) {
	return ERROR_FATAL_FILE_WRITE_FAILED;
      }
      timeout = 0;
    } else {
      timeout++;
    }
  } while (timeout < 2 && total < fileLength);

  return 0;
}


static uint32_t
file_send(int fd, char* filename)
{
  int32_t size = -1;
  uint32_t error = 0;

  BPTR lock = Lock((APTR)filename, ACCESS_READ);
  if (!lock) {
    if (send(fd, (void*)&size, sizeof(size), 0) != sizeof(size)) {
      return ERROR_FATAL_SEND_FAILED;
    }
    return 0;
  }

  struct FileInfoBlock infoBlock;
  Examine(lock, &infoBlock);
  UnLock(lock);

  if (infoBlock.fib_DirEntryType > 0) {
    size = -1;
    error = ERROR_SUCK_ON_DIR;
  } else {
    size = infoBlock.fib_Size;
  }

  if (send(fd, (void*)&size, sizeof(size), 0) != sizeof(size) ||
      send(fd, (void*)&infoBlock.fib_Protection, sizeof(infoBlock.fib_Protection), 0) != sizeof(infoBlock.fib_Protection)) {
    return ERROR_FATAL_SEND_FAILED;
  }

  squirtd_inputFd = Open((APTR)squirtd_filename, MODE_OLDFILE);

  if (!squirtd_inputFd) {
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
	return ERROR_FATAL_SEND_FAILED;
      }
      total += len;
    }

  } while (total < size);

  return error;
}


int
main(int argc, char **argv)
{
  uint32_t error;
  if (argc != 2) {
    fatalError("squirtd: dest_folder\n");
  }

  struct Process *me = (struct Process*)FindTask(0);
  me->pr_WindowPtr = (APTR)-1; // disable requesters

#ifdef __GNUC__
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

 reconnect:
  printf("reconnecting...\n");

  if ((squirtd_connectionFd = accept(squirtd_listenFd, 0, 0)) == -1) {
    fatalError("accept failed\n");
  }

  const LONG socketTimeout = 1000;
  setsockopt(squirtd_connectionFd, SOL_SOCKET, SO_RCVTIMEO, (char*)&socketTimeout, sizeof(socketTimeout));

 again:
  //  printf("waiting for command...\n");
  error = 0;

  struct {
    uint32_t command;
    uint32_t nameLength;
  } command;

  if (recv(squirtd_connectionFd, (void*)&command.command, sizeof(command.command), 0) != sizeof(command.command) ||
     recv(squirtd_connectionFd, (void*)&command.nameLength, sizeof(command.nameLength), 0) != sizeof(command.nameLength)) {
    error = ERROR_FATAL_RECV_FAILED;
    goto error;
  }

  const char* destFolder = argv[1];
  char* filenamePtr;
  int fullPathLen;
  if (command.command == SQUIRT_COMMAND_SQUIRT) {
    int destFolderLen = strlen(destFolder);
    fullPathLen = command.nameLength+destFolderLen;
    squirtd_filename = malloc(fullPathLen+1);
    strcpy(squirtd_filename, destFolder);
    filenamePtr = squirtd_filename+destFolderLen;
  } else {
    fullPathLen = command.nameLength;
    filenamePtr = squirtd_filename = malloc(command.nameLength+1);
  }

  if (recv(squirtd_connectionFd, filenamePtr, command.nameLength, 0) != (int)command.nameLength) {
    error = ERROR_FATAL_RECV_FAILED;
    goto error;
  }

  squirtd_filename[fullPathLen] = 0;


  if (command.command == SQUIRT_COMMAND_CLI) {
    error = exec_run(squirtd_connectionFd, squirtd_filename);
  } else if (command.command == SQUIRT_COMMAND_CD) {
    error = exec_cd(squirtd_filename);
  } else if (command.command == SQUIRT_COMMAND_SUCK) {
    error = file_send(squirtd_connectionFd, squirtd_filename);
  } else if (command.command == SQUIRT_COMMAND_DIR) {
    error = exec_dir(squirtd_connectionFd, squirtd_filename);
  } else if (command.command == SQUIRT_COMMAND_CWD) {
    error = exec_cwd(squirtd_connectionFd);
  } else if (command.command == SQUIRT_COMMAND_SET_INFO) {
    error = file_setInfo(squirtd_connectionFd, squirtd_filename);
  } else if (command.command == SQUIRT_COMMAND_SQUIRT ||
	     command.command == SQUIRT_COMMAND_SQUIRT_TO_CWD) {
    error = file_get(squirtd_connectionFd);
  }

  if (sendU32(squirtd_connectionFd, error) != 0) {
    error = ERROR_FATAL_SEND_FAILED;
  }

  cleanupForNextRun();

  if (error < ERROR_FATAL_ERROR) {
    goto again;
  }

 error:

  cleanupForNextRun();

  if (squirtd_connectionFd > 0) {
    CloseSocket(squirtd_connectionFd);
    squirtd_connectionFd = 0;
  }
  goto reconnect;

  return 0;
}
