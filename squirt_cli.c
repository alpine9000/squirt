#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "squirt.h"
#include "common.h"
#include "argv.h"

static const char* squirt_cliHostname = 0;
static char* currentDir = 0;
static char* line_read = (char *)NULL;
static dir_entry_list_t *squirt_cliDirEntryList;
static char* squirt_cliReadLineBase = 0;

static void
cleanup(void)
{
  if (squirt_cliReadLineBase) {
    free(squirt_cliReadLineBase);
    squirt_cliReadLineBase = 0;
  }

  squirt_dirFreeEntryLists();
  squirt_cliDirEntryList = 0;

  if (currentDir) {
    free(currentDir);
    currentDir = 0;
  }

  if (line_read) {
    free(line_read);
    line_read = 0;
  }
}


static _Noreturn void
cleanupAndExit(int errorCode)
{
  cleanup();
  exit(errorCode);
}


static void
fatalError(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s: ", squirt_argv0);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
  cleanupAndExit(EXIT_FAILURE);
}

static int
changeDir(const char* dir)
{
  if (currentDir) {
    free(currentDir);
    currentDir = 0;
  }

  currentDir = malloc(strlen(dir)+1);
  strcpy(currentDir, dir);
  return 1;
}

static const char*
prompt(void)
{
  static char buffer[256];
  snprintf(buffer, sizeof(buffer), "1.%s> ", currentDir);
  return buffer;
}

static char *
rl_generator(const char *text, int state)
{
  static int list_index, len;

  if (!state) {
    list_index = 0;
    len = strlen(text);
  }

  if (text && text[0] == '!') {
    char* real = rl_filename_completion_function(&text[1], state);
    if (real) {
      if (util_isDirectory(real)) {
	rl_completion_append_character = '/';
      } else {
	rl_completion_append_character = ' ';
      }
      char* fake = malloc(strlen(real)+2);
      sprintf(fake, "!%s", real);
      free(real);
      return fake;
    } else {
      return 0;
    }
  }

  dir_entry_t* ptr;
  if (squirt_cliDirEntryList) {
    ptr = squirt_cliDirEntryList->head;
  } else {
    ptr = 0;
  }

  for (int i = 0; i < list_index && ptr; i++) {
    ptr = ptr->next;
  }
;
  while (ptr) {
    list_index++;
    dir_entry_t* entry = ptr;
    ptr = ptr->next;

    char buffer[256];
    snprintf(buffer, 256, "%s%s", squirt_cliReadLineBase, entry->name);

    if (strncmp(buffer, text, len) == 0) {
      rl_completion_append_character = entry->type > 0 ? '/' : ' ';
      return strdup(buffer);
    }
  }

  return NULL;
}


static char **
rl_completion(const char *text, int start, int end)
{
  (void)start,(void)end;

  if (squirt_cliReadLineBase) {
    free(squirt_cliReadLineBase);
  }

  squirt_cliReadLineBase = strdup(text);

  int ei = strlen(squirt_cliReadLineBase);
  while (ei >= 0 && squirt_cliReadLineBase[ei] != '/' && squirt_cliReadLineBase[ei] != ':') {
    squirt_cliReadLineBase[ei] = 0;
    ei--;
  }
  if (squirt_cliDirEntryList) {
    squirt_dirFreeEntryList(squirt_cliDirEntryList);
  }
  squirt_cliDirEntryList = squirt_dirRead(squirt_cliHostname, squirt_cliReadLineBase);
  //  free(path);
  return rl_completion_matches(text, rl_generator);
}


static char *
rl_gets(void)
{
  if (line_read) {
    free(line_read);
    line_read = (char *)NULL;
  }

  rl_attempted_completion_function = rl_completion;
  line_read = readline(prompt());

  if (line_read && *line_read)
    add_history (line_read);

  return (line_read);
}

static int
squirt_compareFile(const char* one, const char* two)
{
  int identical = 1, fd1 = -1, fd2 = -1;

  if (one == NULL && two == NULL) {
    identical = 1;
    goto cleanup;
  }

  if (one) {
    fd1 = open(one, O_RDONLY|_O_BINARY);
  }

  if (two) {
    fd2 = open(two, O_RDONLY|_O_BINARY);
  }

  if (fd1 == -1 && fd2 == -1) {
    identical = 1;
    goto cleanup;
  } else if (fd1 == -1 || fd2 == -1) {
    identical = 0;
    goto cleanup;
  }

  unsigned char c1, c2;
  int r1, r2;
  do {
    r1 = read(fd1, &c1, sizeof(c1));
    r2 = read(fd2, &c2, sizeof(c2));
    if (r1 != r2 || c1 != c2) {
      identical = 0;
      goto cleanup;
    }
  } while (r1 && r2);

 cleanup:

  if (fd1 >= 0) {
    close(fd1);
  }

  if (fd2 >= 0) {
    close(fd2);
  }

  return identical;
}

static char*
squirt_duplicateFile(const char* from)
{
  int toLength = strlen(from) + strlen(".orig") + 1;
  char *to = malloc(toLength);
  snprintf(to, toLength, "%s.orig", from);

  int fd_to, fd_from;
  char buf[4096];
  ssize_t nread;
  int saved_errno;

  fd_from = open(from, O_RDONLY|_O_BINARY);
  if (fd_from < 0) {
    free(to);
    return 0;
  }

  fd_to = open(to, O_TRUNC|O_WRONLY|O_CREAT|_O_BINARY , 0666);
  if (fd_to < 0) {
    goto out_error;
  }

  while (nread = read(fd_from, buf, sizeof buf), nread > 0) {
    char *out_ptr = buf;
    ssize_t nwritten;

    do {
      nwritten = write(fd_to, out_ptr, nread);

      if (nwritten >= 0) {
	nread -= nwritten;
	out_ptr += nwritten;
      } else if (errno != EINTR) {
	goto out_error;
      }
    } while (nread > 0);
  }

  if (nread == 0) {
    if (close(fd_to) < 0) {
      fd_to = -1;
      goto out_error;
    }
    close(fd_from);

    /* Success! */
    return to;
  }

 out_error:
  saved_errno = errno;

  if (to) {
    free(to);
  }

  close(fd_from);
  if (fd_to >= 0)
    close(fd_to);

  errno = saved_errno;
  return 0;
}

static char*
replace_char(char* str, char find, char replace)
{
  char *current_pos = strchr(str,find);
  while (current_pos){
    *current_pos = replace;
    current_pos = strchr(current_pos,find);
  }
  return str;
}


typedef struct hostfile {
  char* localFilename;
  char* backupFilename;
  char** argv;
  char* remoteFilename;
  struct hostfile* next;
} squirt_hostfile_t;


static void
squirt_freeHostFile(squirt_hostfile_t* file)
{
  if (file) {
    if (file->localFilename) {
      free(file->localFilename);
    }
    if (file->backupFilename) {
      free(file->backupFilename);
    }
    free(file);
  }
}

static squirt_hostfile_t*
squirt_convertFileToHost(const char* hostname, squirt_hostfile_t** list, const char* remote)
{
  squirt_hostfile_t *file = calloc(1, sizeof(squirt_hostfile_t));
  char* local = strdup(remote);
  replace_char(local, '/', '_');
  replace_char(local, ':', '_');

  const char *tempPath = util_getTempFolder();
  int localFilenameLength = strlen(remote) + strlen(tempPath) + 1;
  file->remoteFilename = (char*)remote;
  file->localFilename = malloc(localFilenameLength);
  snprintf(file->localFilename, localFilenameLength, "%s%s", tempPath, local);
  free(local);
  util_mkdir(tempPath, 0777);

  int error = squirt_suckFile(hostname, file->remoteFilename, 0, file->localFilename);
  if (error == -ERROR_SUCK_ON_DIR) {
    fprintf(stderr, "error: failed to access remote directory %s\n", file->remoteFilename);
    free(file);
    return 0;
  } else {
    if (error < 0) {
      unlink(file->localFilename);
    }
    file->backupFilename = squirt_duplicateFile(file->localFilename);
    if (*list) {
      squirt_hostfile_t* ptr = *list;
      while (ptr->next) {
	ptr = ptr->next;
      }
      ptr->next = file;
    } else {
      *list = file;
    }
  }

  return file;
}


static int
squirt_saveFileIfModified(const char* hostname, squirt_hostfile_t* file)
{
  int success = 0;
  if (squirt_compareFile(file->localFilename, file->backupFilename) == 0) {
    success = squirt_file(hostname, file->localFilename, file->remoteFilename, 1, 0) == 0;
  }

  return success;
}

static int
squirt_hostCommand(const char* hostname, int argc, char** argv)
{
  int success = 0;
  (void)argc,(void)argv;

  char command[PATH_MAX*2];
  squirt_hostfile_t* list = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '~' && argv[i][0] != '!' && argv[i][0] != '-' && argv[i][0] != '|' && argv[i][0] != '>') {
      squirt_hostfile_t* hostFile = squirt_convertFileToHost(hostname, &list, argv[i]);
      if (hostFile) {
	hostFile->argv = &argv[i];
	argv[i] = hostFile->localFilename;
      } else {
	goto error;
      }
    }
  }


  command[0] = 0;
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      util_strlcat(command, " ", sizeof(command));
    }
    if (argv[i][0] == '!') {
      util_strlcat(command, &argv[i][1], sizeof(command));
    } else {
      util_strlcat(command, argv[i], sizeof(command));
    }
  }

  success = system(command) == 0;

 error:
  {
    squirt_hostfile_t* file = list;
    while (file) {
      squirt_saveFileIfModified(hostname, file);
      squirt_hostfile_t* save = file;
      file = file->next;
      *save->argv = save->remoteFilename;
      squirt_freeHostFile(save);
    }
  }

  return success;
}

static int
squirt_cliRunCommand(const char* hostname, char* line)
{
  int code;
  int end = strlen(line);
  while (end >= 0 && line[end] == ' ') {
    line[end] = 0;
    end--;
  }
  char** argv = argv_build((char*)line);
  int argc = argv_argc(argv);

  if (strcmp("endcli", argv[0]) == 0) {
    cleanupAndExit(EXIT_SUCCESS);
  } else if (argc == 2 && strcmp("cd", argv[0]) == 0) {
    if (squirt_execCmd(hostname, argc, argv) == 0) {
      changeDir(argv[1]);
      code = 1;
    } else {
      printf("cd: %s failed\n", argv[1]);
      code = 0;
    }
  } else if (argv[0][0] == '!') {
    code = squirt_hostCommand(hostname, argc, argv);
  } else {
    code = squirt_execCmd(hostname, argc, argv);
  }

  argv_free(argv);
  return code;
}



static void
squirt_writeHistory(void)
{
  write_history(util_getHistoryFile());
  cleanupAndExit(EXIT_SUCCESS);
}


int
squirt_cli(int argc, char* argv[])
{
  (void)argc,(void)argv;

  squirt_cliDirEntryList = 0;
  squirt_cliReadLineBase = 0;

  if (argc != 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", squirt_argv0);
  }

  squirt_cliHostname = argv[1];

  using_history();
  read_history(util_getHistoryFile());

  util_onCtrlC(squirt_writeHistory);


  do {
    const char* cwd = squirt_cwdRead(squirt_cliHostname);
    if (!cwd) {
      fatalError("failed to get cwd");
    }
    changeDir(cwd);
    free((void*)cwd);
    char* command = rl_gets();
    if (command && strlen(command)) {
      add_history(command);
      squirt_cliRunCommand(argv[1], command);
    }
  } while (1);

  exit(EXIT_SUCCESS);
  return 0;
}
