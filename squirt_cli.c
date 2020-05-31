#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "squirt.h"
#include "common.h"
#include "argv.h"

static const char* squirt_cliHostname = 0;
static char* currentDir = 0;
static char* line_read = (char *)NULL;
static dir_entry_list_t dirEntryList = {0};
static char* squirt_cliReadLineBase = 0;

static void
cleanup(void)
{
  squirt_dirFreeEntryList(&dirEntryList);

  if (currentDir) {
    free(currentDir);
    currentDir = 0;
  }

  if (line_read) {
    free(line_read);
    line_read = 0;
  }
}


static void
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

  dir_entry_t* ptr = dirEntryList.head;

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
  rl_attempted_completion_over = 1;
  squirt_cliReadLineBase = strdup(text);
  int ei = strlen(squirt_cliReadLineBase);
  while (ei >= 0 && squirt_cliReadLineBase[ei] != '/' && squirt_cliReadLineBase[ei] != ':') {
    squirt_cliReadLineBase[ei] = 0;
    ei--;
  }
  dirEntryList = squirt_dirRead(squirt_cliHostname, squirt_cliReadLineBase);
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
  int identical = 1;
  int fd1 = open(one, O_RDONLY);
  int fd2 = open(two, O_RDONLY);

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

  fd_from = open(from, O_RDONLY);
  if (fd_from < 0) {
    free(to);
    return 0;
  }

  fd_to = open(to, O_TRUNC|O_WRONLY|O_CREAT , 0666);
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

  int localFilenameLength = strlen(remote) + strlen("/tmp/.squirt.") + 1;
  file->remoteFilename = (char*)remote;
  file->localFilename = malloc(localFilenameLength);
  snprintf(file->localFilename, localFilenameLength, "/tmp/.squirt/%s", local);
  util_mkdir("/tmp/.squirt", 0777);

#if 0
  if (squirt_suckFile(hostname, file->remoteFilename, 0, file->localFilename) == 0) {
    goto error;
  } else {

    if ((file->backupFilename = squirt_duplicateFile(file->localFilename)) != 0) {
      if (*list) {
	squirt_hostfile_t* ptr = *list;
	while (ptr->next) {
	  ptr = ptr->next;
	}
	ptr->next = file;
      } else {
	*list = file;
      }
      return file;
    }
  }
 error:
  fprintf(stderr, "failed to convert %s to local file\n", remote);
  squirt_freeHostFile(file);
  return 0;
#else
  if (!squirt_suckFile(hostname, file->remoteFilename, 0, file->localFilename)) {
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

  return file;

#endif

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
    if (argv[i][0] != '!' && argv[i][0] != '-' && argv[i][0] != '|' && argv[i][0] != '>') {
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
      strlcat(command, " ", sizeof(command));
    }
    if (argv[i][0] == '!') {
      strlcat(command, &argv[i][1], sizeof(command));
    } else {
      strlcat(command, argv[i], sizeof(command));
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
  int end = strlen(line);
  while (end >= 0 && line[end] == ' ') {
    line[end] = 0;
    end--;
  }
  char** argv = argv_build((char*)line);
  int argc = argv_argc(argv);

  if (argc == 2 && strcmp("cd", argv[0]) == 0) {
    if (squirt_execCmd(hostname, argc, argv) == 0) {
      changeDir(argv[1]);
      return 1;
    }
    printf("cd: %s failed\n", argv[1]);
    return 0;
  } else if (argv[0][0] == '!') {
    return squirt_hostCommand(hostname, argc, argv);
  } else {
    return squirt_execCmd(hostname, argc, argv);
  }
}


static void
squirt_writeHistory(int signal)
{
  (void)signal;
  write_history(util_getHistoryFile());
  cleanupAndExit(EXIT_SUCCESS);
}

int
squirt_cli(int argc, char* argv[])
{
  (void)argc,(void)argv;

  memset(&dirEntryList, 0, sizeof(dirEntryList));

  if (argc != 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", squirt_argv0);
  }

  squirt_cliHostname = argv[1];

  using_history();
  read_history(util_getHistoryFile());
  signal(SIGINT, squirt_writeHistory);

  do {
    const char* cwd = squirt_cwdRead(squirt_cliHostname);
    if (!cwd) {
      fatalError("failed to get cwd");
    }
    changeDir(cwd);
    char* command = rl_gets();
    if (command && strlen(command)) {
      add_history(command);
      squirt_cliRunCommand(argv[1], command);
    }
  } while (1);

  exit(EXIT_SUCCESS);
  return 0;
}
