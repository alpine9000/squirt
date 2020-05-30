#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
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
  } else {
    return squirt_execCmd(hostname, argc, argv);
  }
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


  do {
    const char* cwd = squirt_cwdRead(squirt_cliHostname);
    if (!cwd) {
      fatalError("failed to get cwd");
    }
    changeDir(cwd);
    char* command =  rl_gets();
    if (command && strlen(command)) {
      squirt_cliRunCommand(argv[1], command);
    }
  } while (1);

  exit(EXIT_SUCCESS);
  return 0;
}
