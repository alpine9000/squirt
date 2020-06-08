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


  printf("set: %s %d %d %d\n", dir_formatDateTime(temp), temp->ds.days, temp->ds.mins, temp->ds.ticks);
  int error =  protect_file(path, temp->prot, &temp->ds);

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
	  fatalError("unabled to read exall data for %s\n", filename);
	}
	if (!exall_identicalExAllData(temp, entry)) {
	  update = UPDATE_EXALL;
	}
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

static void
restore_operation(const char* filename, void* data)
{
  if (strcmp(filename, ".") == 0 ||
      strcmp(filename, "..") == 0 ||
      strcmp(filename, ".git") == 0 ||
      strcmp(filename, SQUIRT_EXALL_INFO_DIR) == 0) {
    return;
  }

  dir_entry_t* entry = data;
  char* path = restore_fullPath(filename);

  int isDir = util_isDirectory(filename);
  restore_update_t update = restore_remoteFileNeedsUpdating(filename, isDir, entry);

  if (isDir) {
    switch (update) {
    case UPDATE_CREATE:
      printf("Creating DIR: %s %s\n", filename, path);
      char buffer[PATH_MAX];
      snprintf(buffer, sizeof(buffer), "makedir \"%s\"", filename);
      if (util_exec(buffer) != 0) {
	fprintf(stderr, "failed to create %s\n", filename);
      }
      break;
    case UPDATE_EXALL:
      printf("Updating INFO: %s %s\n", filename, path);
      if (restore_updateExAll(filename, path) != 0) {
	fatalError("failed to update ExAll for %s", filename);
      }
      break;
    case UPDATE_NOUPDATE:
      //printf("\xE2\x9C\x85 %s\n", path); // utf-8 tick
      break;
    }
    restore_restoreDir(filename);
  } else {
    switch (update) {
    case UPDATE_CREATE:
      printf("Creating FILE: %s %s\n", filename, path);
      if (squirt_file(filename, path, 1, 1) != 0) {
	fatalError("failed to restore %s\n", path);
      }
      break;
    case UPDATE_EXALL:
      printf("Updating INFO: %s %s\n", filename, path);
      if (restore_updateExAll(filename, path) != 0) {
	fatalError("failed to update ExAll for %s", filename);
      }
      break;
    case UPDATE_NOUPDATE:
      //      printf("%s - updated\n", path);
      //      printf("\xE2\x9C\x85 %s\n", path); // utf-8 tick
      break;
    }
  }

  free(path);
}


static void
restore_list(dir_entry_list_t* list)
{
  util_dirOperation(".", restore_operation, list->head);

  dir_entry_t* entry = list->head;
  while (entry) {
    struct stat st;
    if (stat(entry->name, &st) != 0) {
      char* path = restore_fullPath(entry->name);
      printf("PROB NEEDS DELETING %s (%s) (%s)\n", path, entry->name, getcwd(0, 0));
      free(path);
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
       //    {"prune",    no_argument, &restore_prune, 'p'},
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
    restore_skipFile = backup_loadSkipFile(skipFile);
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

  printf("dir = %s\n", dir);
  printf("cwd = %s\n",  getcwd(0, 0));

  if (dir) {
    restore_restoreDir(dir);
  }


  printf("\nrestore complete!\n");
}
