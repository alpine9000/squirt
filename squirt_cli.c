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

  if (fd1 == -1 || fd2 == -1) {
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

static const char*
squirt_duplicateFile(const char* from)
{
  static char to[PATH_MAX];
  snprintf(to, sizeof(to), "%s.orig", from);

  int fd_to, fd_from;
  char buf[4096];
  ssize_t nread;
  int saved_errno;

  fd_from = open(from, O_RDONLY);
  if (fd_from < 0) {
    return 0;
  }

  fd_to = open(to, O_TRUNC | O_WRONLY | O_CREAT , 0666);
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

static int
squirt_hostCommand(const char* hostname, const char* hostCommand, int argc, char** argv)
{
  (void)argc,(void)argv;
  char filename[PATH_MAX];
  char command[PATH_MAX*2];
  char* local = strdup(argv[1]);
  replace_char(local, '/', '_');
  replace_char(local, ':', '_');

  snprintf(filename, sizeof(filename), "/tmp/.squirt/%s", local);
  util_mkdir("/tmp/.squirt", 0777);
  if (squirt_suckFile(hostname, argv[1], 0, filename) != 0) {
    const char* backup;
    if ((backup = squirt_duplicateFile(filename)) != 0) {
      snprintf(command, sizeof(command), "%s %s", hostCommand, filename);
      int success = system(command) == 0;
      if (success) {
	if (squirt_compareFile(filename, backup) == 0) {
	  success = squirt_file(hostname, filename, argv[1], 1, 0) == 0;
	}
      }
      return success;
    }
  }

  return 0;
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
  } else if (argc == 2 && strcmp("emacs", argv[0]) == 0) {
    return squirt_hostCommand(hostname, "emacs -nw", argc, argv);
  } else if (argc == 2 && argv[0][0] == '!') {
    return squirt_hostCommand(hostname, &argv[0][1], argc, argv);
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
