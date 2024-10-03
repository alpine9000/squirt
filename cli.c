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

#include "main.h"
#include "common.h"
#include "argv.h"

typedef struct hostfile {
  char* localFilename;
  char* backupFilename;
  char** argv;
  char* remoteFilename;
  struct hostfile* next;
  dir_datestamp_t dateStamp;
  uint32_t remoteProtection;
} cli_hostfile_t;

static char* cli_currentDir = 0;
static dir_entry_list_t *cli_dirEntryList = 0;
static char* cli_readLineBase = 0;


void
cli_cleanup(void)
{
  srl_write_history();

  if (cli_readLineBase) {
    free(cli_readLineBase);
    cli_readLineBase = 0;
  }

  dir_freeEntryLists();
  cli_dirEntryList = 0;

  if (cli_currentDir) {
    free(cli_currentDir);
    cli_currentDir = 0;
  }
}


static int
cli_changeDir(const char* dir)
{
  if (cli_currentDir) {
    free(cli_currentDir);
    cli_currentDir = 0;
  }

  cli_currentDir = malloc(strlen(dir)+1);
  strcpy(cli_currentDir, dir);
  return 1;
}


static const char*
cli_prompt(void)
{
  static char buffer[256];
  snprintf(buffer, sizeof(buffer), "1.%s> ", cli_currentDir);
  return buffer;
}


static int
cli_compareFile(const char* one, const char* two)
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
cli_duplicateFile(const char* from)
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
cli_replaceChar(char* str, char find, char replace)
{
  char *current_pos = strchr(str,find);
  while (current_pos){
    *current_pos = replace;
    current_pos = strchr(current_pos,find);
  }
  return str;
}


static void
cli_freeHostFile(cli_hostfile_t* file)
{
  if (file) {
    if (file->localFilename) {
      unlink(file->localFilename);
      free(file->localFilename);
    }
    if (file->backupFilename) {
      unlink(file->backupFilename);
      free(file->backupFilename);
    }
    free(file);
  }
}


static cli_hostfile_t*
cli_convertFileToHost(cli_hostfile_t** list, const char* remote)
{
  cli_hostfile_t *file = calloc(1, sizeof(cli_hostfile_t));
  char* local = strdup(remote);
  cli_replaceChar(local, ':', '/');

  const char *tempPath = util_getTempFolder();
  int localFilenameLength = strlen(remote) + strlen(tempPath) + 1;
  file->remoteFilename = (char*)remote;
  file->localFilename = malloc(localFilenameLength);
  snprintf(file->localFilename, localFilenameLength, "%s%s", tempPath, local);
  free(local);
  util_mkpath(file->localFilename);

  memset(&file->dateStamp, 0, sizeof(file->dateStamp));
  int error = squirt_suckFile(file->remoteFilename, 0, 0, file->localFilename, &file->remoteProtection);
  if (error == -ERROR_SUCK_ON_DIR) {
    fprintf(stderr, "error: failed to access remote directory %s\n", file->remoteFilename);
    free(file);
    return 0;
  } else {
    if (error < 0) {
      unlink(file->localFilename);
    }
    file->backupFilename = cli_duplicateFile(file->localFilename);
    if (*list) {
      cli_hostfile_t* ptr = *list;
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
cli_saveFileIfModified(cli_hostfile_t* file)
{
  int success = 0;
  if (cli_compareFile(file->localFilename, file->backupFilename) == 0) {
    success = squirt_file(file->localFilename, 0, file->remoteFilename, 1, 0) == 0;
    if (success) {
      success = protect_file(file->remoteFilename, file->remoteProtection, 0);
    }
  }

  return success;
}


static int
cli_hostCommand(int argc, char** argv)
{
  int success = 0;
  (void)argc,(void)argv;

  cli_hostfile_t* list = 0;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '~' && argv[i][0] != '!' && argv[i][0] != '-' && argv[i][0] != '|' && argv[i][0] != '>') {
      cli_hostfile_t* hostFile = cli_convertFileToHost(&list, argv[i]);
      if (hostFile) {
	hostFile->argv = &argv[i];
	argv[i] = hostFile->localFilename;
      } else {
	goto error;
      }
    }
  }

  char** newArgv = malloc(sizeof(char*)*(argc+1));

  for (int i = 0; i < argc; i++) {
    char* ptr = argv[i][0] == '!' ? &argv[i][1] : argv[i];
    newArgv[i] = ptr;
  }
  newArgv[argc] = 0;

  success = util_system(newArgv);
  free(newArgv);

 error:
  {
    cli_hostfile_t* file = list;
    while (file) {
      cli_saveFileIfModified(file);
      cli_hostfile_t* save = file;
      file = file->next;
      *save->argv = save->remoteFilename;
      cli_freeHostFile(save);
    }
  }

  util_rmdir(util_getTempFolder());

  return success;
}


static int
cli_runCommand(char* line)
{
  int code;
  int end = strlen(line) - 1;
  while (end >= 0 && line[end] == ' ') {
    line[end] = 0;
    end--;
  }

  // Remove trailing newline if present
  line[strcspn(line, "\n")] = 0;

  // Check for Amiga-style directory navigation
  if (line[0] == '/') {
    int slash_count = 0;
    while (line[slash_count] == '/') {
      slash_count++;
    }
    
    for (int i = 0; i < slash_count; i++) {
      if (exec_cmd(2, (char*[]){"cd", "/"}) != 0) {
        fprintf(stderr, "cd: / failed\n");
        return 0;
      }
    }

    const char* new_path = cwd_read();
    if (new_path != NULL) {
      cli_changeDir(new_path);
      return 1;
    } else {
      fprintf(stderr, "Failed to get new working directory\n");
      return 0;
    }
  } else if (strncasecmp(line, "cd ", 3) == 0 || 
             (strchr(line, ':') != NULL && strchr(line, ' ') == NULL) || 
             line[end] == '/' || 
             (strchr(line, '/') != NULL && strchr(line, ' ') == NULL)) {
    char* cmd = strdup(line);
    char* arg = cmd;

    // If the command starts with "CD " or "cd ", skip past it (case-insensitive)
    if (strncasecmp(cmd, "cd ", 3) == 0) {
      arg += 3;
    }

    // Remove trailing slash if present, unless it's the only character
    if (strlen(arg) > 1 && arg[strlen(arg) - 1] == '/') {
      arg[strlen(arg) - 1] = '\0';
    }

    // Attempt to change directory
    if (exec_cmd(2, (char*[]){"cd", arg}) == 0) {
      const char* new_path = cwd_read();
      if (new_path) {
        cli_changeDir(new_path);
        free(cmd);
        code = 1;
        return code;
      }
    }
    fprintf(stderr, "cd: %s failed\n", arg);
    free(cmd);
    code = 0;
    return code;
  }

  // Handle other commands
  char** argv = argv_build((char*)line);
  int argc = argv_argc(argv);

  if (strcmp("endcli", argv[0]) == 0) {
    main_cleanupAndExit(EXIT_SUCCESS);
  } else if (argv[0][0] == '!') {
    code = cli_hostCommand(argc, argv);
  } else {
    code = exec_cmd(argc, argv);
  }

  argv_free(argv);
  return code;
}



static void
cli_onExit(void)
{
  main_cleanupAndExit(EXIT_SUCCESS);
}


static char*
cli_completeGenerator(int* list_index, const char* text, int len)
{
  dir_entry_t* ptr;
  if (cli_dirEntryList) {
    ptr = cli_dirEntryList->head;
  } else {
    ptr = 0;
  }

  for (int i = 0; i < *list_index && ptr; i++) {
    ptr = ptr->next;
  }

  while (ptr) {
    (*list_index)++;
    dir_entry_t* entry = ptr;
    ptr = ptr->next;

    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "%s%s", cli_readLineBase, entry->name);

    if (strncmp(buffer, text, len) == 0) {
      rl_completion_append_character = entry->type > 0 ? '/' : ' ';
      return strdup(buffer);
    }
  }

  return NULL;
}


static void
cli_completeHook(const char* text)
{
  if (cli_readLineBase) {
    free(cli_readLineBase);
  }

  cli_readLineBase = strdup(text);

  int ei = strlen(cli_readLineBase);
  while (ei >= 0 && cli_readLineBase[ei] != '/' && cli_readLineBase[ei] != ':') {
    cli_readLineBase[ei] = 0;
    ei--;
  }
  if (cli_dirEntryList) {
    dir_freeEntryList(cli_dirEntryList);
  }

  cli_dirEntryList = dir_read(cli_readLineBase);
}


void
cli_main(int argc, char* argv[])
{
  (void)argc,(void)argv;

  cli_dirEntryList = 0;
  cli_readLineBase = 0;

  if (argc != 2) {
    fatalError("incorrect number of arguments\nusage: %s hostname", main_argv0);
  }


  util_connect(argv[1]);
  util_onCtrlC(cli_onExit);

  srl_init(cli_prompt, cli_completeHook, cli_completeGenerator);

  do {
    const char* cwd = cwd_read();
    if (!cwd) {
      fatalError("failed to get cwd");
    }
    cli_changeDir(cwd);
    free((void*)cwd);
    char* command = srl_gets();
    if (command && strlen(command)) {
      cli_runCommand(command);
    }
  } while (1);
}
