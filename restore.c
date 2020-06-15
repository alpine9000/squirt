#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <utime.h>
#include <limits.h>
#include <getopt.h>
#include <sys/stat.h>

#include "main.h"
#include "common.h"
#include "exall.h"

typedef enum {
  UPDATE_NOUPDATE,
  UPDATE_CREATE,
  UPDATE_EXALL,
} restore_update_t;

static char* restore_currentDir = 0;
static char* restore_dirBuffer = 0;
static char* restore_skipFile = 0;
static int restore_quiet = 0;

static void
restore_restoreDir(const char* remote);

void
restore_cleanup()
{
  if (restore_currentDir) {
    free(restore_currentDir);
    restore_currentDir = 0;
  }

  if (restore_dirBuffer) {
    free(restore_dirBuffer);
    restore_dirBuffer = 0;
  }

  if (restore_skipFile) {
    free(restore_skipFile);
    restore_skipFile = 0;
  }

}


static char*
restore_fullPath(const char* name)
{
  if (!restore_currentDir) {
    return strdup(name);
  }

  char* path = malloc(strlen(restore_currentDir) + strlen(name) + 2);
  if (!path) {
    return NULL;
  }
  if (restore_currentDir[strlen(restore_currentDir)-1] != ':') {
    sprintf(path, "%s/%s", restore_currentDir, name);
  } else {
    sprintf(path, "%s%s", restore_currentDir, name);
  }
  return path;
}


static char*
restore_pushDir(const char* dir)
{
  if (restore_currentDir) {
    char* newDir = restore_fullPath(dir);
    if (restore_currentDir) {
      free(restore_currentDir);
    }
    restore_currentDir = newDir;
  } else {
    restore_currentDir = malloc(strlen(dir)+1);
    strcpy(restore_currentDir, dir);
  }

  if (util_cd(restore_currentDir) != 0) {
    fatalError("unable to restore %s", restore_currentDir);
  }

  char* safe = util_safeName(dir);
  if (!safe) {
    fatalError("failed to create safe name");
  }

  char* cwd = getcwd(0, 0);

  if (chdir(safe) == -1) {
    fatalError("unable to chdir to %s", safe);
  }

  free(safe);

  return cwd;
}


static void
restore_popDir(char* cwd)
{
  for (int i = strlen(restore_currentDir)-1; i >= 0; --i) {
    if (restore_currentDir[i] == '/' || (i > 0 && restore_currentDir[i-1] == ':')) {
      restore_currentDir[i] = 0;
      break;
    }
  }

  if (chdir(cwd)) {
    fatalError("failed to cd to %s", cwd);
  }
  free((void*)cwd);
}


static int
restore_updateExAll(const char* filename, const char* path)
{
  dir_entry_t *temp = dir_newDirEntry();
  if (!exall_readExAllData(temp, filename)) {
    fatalError("unabled to read exall data for %s\n", filename);
  }

  int error =  protect_file(path, temp->prot, &temp->ds);

  if (temp->comment && strlen(temp->comment) > 0) {
    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "filenote \"%s\" \"%s\"", path, temp->comment);
    if (util_exec(buffer) != 0) {
      fprintf(stderr, "failed to set comment %s\n", filename);
    }
  }

    dir_freeEntry(temp);


  return error;
}


static restore_update_t
restore_remoteFileNeedsUpdating(const char* filename, int isDir, dir_entry_t* list)
{
  restore_update_t update = UPDATE_NOUPDATE;
  int found = 0;
  dir_entry_t* entry = list;

  while (entry) {
    if (strcmp(entry->name, filename) == 0) {
      found = 1;
      break;
    }
    entry = entry->next;
  }

  if (found) {
    dir_entry_t *temp = dir_newDirEntry();
    struct stat st;
    if (stat(filename, &st) == 0) {
      if (isDir || st.st_size == (off_t)entry->size) {
	if (!exall_readExAllData(temp, filename)) {
	  fatalError("unable to read exall data for %s\n", filename);
	}
	if (!exall_identicalExAllData(temp, entry)) {
	  update = UPDATE_EXALL;
	}
      } else if (st.st_size != (off_t)entry->size) {
	update = UPDATE_CREATE;
      }
    } else {
      fatalError("unable to read %s\n", filename);
    }
    dir_freeEntry(temp);
  } else {
    update = UPDATE_CREATE;
  }

  return update;
}

void
restore_printProgress(const char* filename, struct timeval* start, uint32_t total, uint32_t fileLength)
{
  (void)start;
  int percentage;

  if (fileLength) {
    percentage = (((uint64_t)total*(uint64_t)100)/(uint64_t)fileLength);
  } else {
    percentage = 100;
  }

#ifndef _WIN32
  printf("\r%c[K", 27);
#else
  printf("\r");
#endif
  fflush(stdout);
  if (percentage >= 100) {
    printf("\xE2\x9C\x85 "); // utf-8 tick
  } else {
    printf("\xE2\x8C\x9B "); // utf-8 hourglass
  }

  printf("%s %3d%% ", filename, percentage);

#ifndef _WIN32
  fflush(stdout);
#endif
}

static void
restore_operation(const char* filename, void* data)
{
  if (strcmp(filename, ".") == 0 ||
      strcmp(filename, "..") == 0 ||
      strcmp(filename, SQUIRT_EXALL_INFO_DIR) == 0) {
    return;
  }

  dir_entry_t* entry = data;
  char* path = restore_fullPath(filename);

  int isDir = util_isDirectory(filename);
  restore_update_t update = restore_remoteFileNeedsUpdating(filename, isDir, entry);

  if (isDir) {
    if (update == UPDATE_CREATE) {
      char buffer[PATH_MAX];
      snprintf(buffer, sizeof(buffer), "makedir \"%s\"", path);
      if (util_exec(buffer) != 0) {
	fatalError("failed to create %s", filename);
      }
    }
    restore_restoreDir(filename);
    switch (update) {
    case UPDATE_CREATE:
    case UPDATE_EXALL:
      printf("\xE2\x8C\x9B %s restoring...", path); // utf-8 hourglass
      fflush(stdout);

      if (restore_updateExAll(filename, path) != 0) {
	fatalError("failed to update ExAll for %s", filename);
      }
#ifndef _WIN32
	printf("\r%c[K", 27);
#else
	printf("\r");
#endif
	printf("\xE2\x9C\x85 %s restoring...done\n", path); // utf-8 tick
      break;
    case UPDATE_NOUPDATE:
      if (!restore_quiet) {
	printf("\xE2\x9C\x85 %s\n", path); // utf-8 tick
      }
      break;
    }
  } else {
    switch (update) {
    case UPDATE_CREATE:
    case UPDATE_EXALL:
      printf("\xE2\x8C\x9B %s restoring...", path); // utf-8 hourglass
      fflush(stdout);
      char updateMessage[PATH_MAX];
      snprintf(updateMessage, sizeof(updateMessage), "%s restoring...", path);
      if (squirt_file(filename, updateMessage, path, 1, restore_printProgress) != 0) {
	fatalError("failed to restore %s\n", path);
      }
      if (restore_updateExAll(filename, path) != 0) {
	fatalError("failed to update ExAll for %s", filename);
      }
#ifndef _WIN32
	printf("\r%c[K", 27);
#else
	printf("\r");
#endif
	printf("\xE2\x9C\x85 %s restoring...done\n", path); // utf-8 tick
      break;
    case UPDATE_NOUPDATE:
      if (!restore_quiet) {
	printf("\xE2\x9C\x85 %s\n", path); // utf-8 tick
      }
      break;
    }
  }

  free(path);
}


static int
restore_skip(const char* filename)
{
  int skipFile = 0;
  if (restore_skipFile) {
    const char* path = restore_fullPath(filename);
    char* found = strstr(restore_skipFile, path);
    if (found) {
      found += strlen(path);
      skipFile = *found == 0 || *found == '\n' || *found == '\r';
    }
    free((void*)path);
  }
  return skipFile;
}

static void
restore_list(dir_entry_list_t* list)
{
  util_dirOperation(".", restore_operation, list->head);

  dir_entry_t* entry = list->head;
  while (entry) {
    struct stat st;
    if (!restore_skip(entry->name)) {
      if (stat(entry->name, &st) != 0) {
	char* path = restore_fullPath(entry->name);
	char* cwd = getcwd(0, 0);
	if (!cwd) {
	  perror("cwd failed");
	}
	printf("PROB NEEDS DELETING %s (%s) (%s)\n", path, entry->name, cwd);
	free(path);
      }
    }
    entry = entry->next;
  }
}

static void
restore_restoreDir(const char* remote)
{
  char* local = restore_pushDir(remote);

  if (dir_process(restore_currentDir, restore_list) != 0) {
    fatalError("unable to read %s", remote);
  }

  restore_popDir(local);
}


_Noreturn static void
restore_usage(void)
{
  fatalError("invalid arguments\nusage: %s hostname dir_name", main_argv0);
}

void
restore_main(int argc, char* argv[])
{
  const char* hostname = 0;
  char* path = 0;
  char* skipFile = 0;
  int argvIndex = 1;

  while (argvIndex < argc) {
    static struct option long_options[] =
      {
       {"quiet",    no_argument, &restore_quiet, 'q'},
       {"skipfile", required_argument, 0, 's'},
       {0, 0, 0, 0}
      };
    int option_index = 0;
    int c = getopt_long (argc, argv, "", long_options, &option_index);

    if (c != -1) {
      argvIndex = optind;
      switch (c) {
      case 0:
	break;
      case 's':
	if (optarg == 0 || strlen(optarg) == 0) {
	  restore_usage();
	}
	skipFile = optarg;
	break;
      case '?':
      default:
	restore_usage();
	break;
      }
    } else {
      if (hostname == 0) {
	hostname = argv[argvIndex];
      } else {
	path = argv[argvIndex];
      }
      argvIndex++;
      optind++;
    }
  }

  if (!hostname || !path) {
    restore_usage();
  }

  if (skipFile) {
    restore_skipFile = backup_loadSkipFile(skipFile, 0);
  } else {
    restore_skipFile = backup_loadSkipFile(".skip", 1);
  }

  util_connect(hostname);

  char* token = strtok(path, ":");
  char* dir = 0;
  if (token) {
    dir = token;
    token = strtok(0, "/");
    if (token) {
      restore_dirBuffer = malloc(strlen(dir)+2);
      if (!restore_dirBuffer) {
	fatalError("malloc failed");
      }
      sprintf(restore_dirBuffer, "%s:", dir);
      free(restore_pushDir(restore_dirBuffer));
      do {
	dir = token;
	token = strtok(0, "/");
	if (token) {
	  free(restore_pushDir(dir));
	}
      } while (token);
    } else {
      restore_dirBuffer = malloc(strlen(dir)+2);
      if (!restore_dirBuffer) {
	fatalError("malloc failed");
      }
      sprintf(restore_dirBuffer, "%s:", dir);
      dir = restore_dirBuffer;
    }
  }

  if (dir) {
    restore_restoreDir(dir);
  }

  printf("\nrestore complete!\n");
}
