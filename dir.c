#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "main.h"
#include "common.h"

static dir_entry_list_t* dir_entryLists = 0;


dir_entry_list_t*
dir_newEntryList(void)
{
  dir_entry_list_t* list = calloc(1, sizeof(dir_entry_list_t));
  list->next = NULL;
  list->prev = NULL;

  if (dir_entryLists == 0) {
    dir_entryLists = list;
  } else {
    dir_entry_list_t* ptr = list;
    while (ptr->next) {
      ptr = ptr->next;
    }
    ptr->next = list;
    list->prev = ptr;
  }
  return list;
}


void
dir_cleanup(void)
{

}


dir_entry_t*
dir_newDirEntry(void)
{
  return calloc(1, sizeof(dir_entry_t));
}


static void
dir_pushDirEntry(dir_entry_list_t* list, const char* name, int32_t type, uint32_t size, uint32_t prot, uint32_t days, uint32_t mins, uint32_t ticks, const char* comment)
{
  dir_entry_t* entry = dir_newDirEntry();

  if (list->tail == 0) {
    list->head = list->tail = entry;
  } else {
    list->tail->next = entry;
    list->tail = entry;
  }

  entry->next = 0;
  entry->name = name;
  entry->type = type;
  entry->prot = prot;
  entry->ds.days = days;
  entry->ds.mins = mins;
  entry->ds.ticks = ticks;
  entry->size = size;
  entry->comment = comment;
}


void
dir_freeEntry(dir_entry_t* ptr)
{
  if (ptr) {
    if (ptr->name) {
      free((void*)ptr->name);
    }
    if (ptr->comment) {
      free((void*)ptr->comment);
    }
    free(ptr);
  }
}


void
dir_freeEntryLists(void)
{
  dir_entry_list_t* ptr = dir_entryLists;

  while (ptr) {
    dir_entry_list_t* save = ptr;
    dir_entry_t* entry = save->head;
    while (entry) {
      dir_entry_t* p = entry;
      entry = entry->next;
      dir_freeEntry(p);
    }

    ptr = ptr->next;
    free(save);
  }
  dir_entryLists = 0;
}


void
dir_freeEntryList(dir_entry_list_t* list)
{
  dir_entry_list_t* ptr = dir_entryLists;
  while (ptr) {
    if (ptr == list) {
      if (ptr->prev != NULL) {
	ptr->prev->next = ptr->next;
      } else {
	dir_entryLists = ptr->next;
      }

      if (ptr->next != NULL) {
	ptr->next->prev = ptr->prev;
      }
      break;
    }
    ptr = ptr->next;
  }

  dir_entry_t* entry = list->head;

  while (entry) {
    dir_entry_t* p = entry;
    entry = entry->next;
    dir_freeEntry(p);
  }

  free(list);
}


static void
dir_printProtectFlags(dir_entry_t* entry)
{
  char bits[] = {'d', 'e', 'w', 'r', 'a', 'p', 's', 'h'};
  uint32_t prot = (entry->prot ^ 0xF) & 0xFF;
  for (int i = 7; i >= 0; i--) {
    if (prot & (1<<i)) {
      printf("%c", bits[i]);
    } else {
      printf("-");
    }
  }
}


char*
dir_formatDateTime(dir_entry_t* entry)
{
  struct timeval tv;
  time_t nowtime;
  struct tm *nowtm;
  static char tmbuf[64];

  int sec = entry->ds.ticks / 50;
  tv.tv_sec = (DIR_AMIGA_EPOC_ADJUSTMENT_DAYS*24*60*60)+(entry->ds.days*(24*60*60)) + (entry->ds.mins*60) + sec;
  tv.tv_usec = (entry->ds.ticks - (sec * 50)) * 200;
  nowtime = tv.tv_sec;
  nowtm = gmtime(&nowtime);
  strftime(tmbuf, sizeof tmbuf, "%m-%d-%y %H:%M:%S", nowtm);
  return tmbuf;
}


static void
squirt_dirPrintEntryList( dir_entry_list_t* list)
{
  dir_entry_t* entry = list->head;
  int maxSizeLength = 0;

  while (entry) {
    char buffer[255];
    snprintf(buffer, sizeof(buffer), "%s", util_formatNumber(entry->size));
    entry->renderedSizeLength = strlen(buffer);
    if (entry->renderedSizeLength > maxSizeLength) {
      maxSizeLength = entry->renderedSizeLength;
    }
    entry = entry->next;
  }

  entry = list->head;
  while (entry) {
    dir_printProtectFlags(entry);

    for (int i = 0; i < maxSizeLength-entry->renderedSizeLength + 3; i++) {
      putchar(' ');
    }

    if (printf("%s %s %s%c", util_formatNumber(entry->size), dir_formatDateTime(entry), entry->name, entry->type > 0 ? '/' : ' ') < 0) {
      perror("printf");
    }
    if (entry->comment) {
      printf(" (%s)", entry->comment);
    }
    printf("\n");
    entry = entry->next;
  }
}


static uint32_t
dir_getDirEntry(dir_entry_list_t* entryList)
{
  uint32_t nameLength;

  if (util_recvU32(main_socketFd, &nameLength) != 0) {
    fatalError("failed to read name length");
  }

  if (nameLength == 0xFFFFFFFF) {
    return 0;
  }

  char* buffer = util_recvLatin1AsUtf8(main_socketFd, nameLength);

  if (!buffer) {
    fprintf(stderr, "failed to read name\n");
    return 0;
  }

  int32_t type;
  if (util_recv32(main_socketFd, &type) != 0) {
    fatalError("failed to read type");
  }

  uint32_t size;
  if (util_recvU32(main_socketFd, &size) != 0) {
    fatalError("failed to read file size");
  }

  uint32_t prot;
  if (util_recvU32(main_socketFd, &prot) != 0) {
    fatalError("failed to read file prot");
  }

  uint32_t days;
  if (util_recvU32(main_socketFd, &days) != 0) {
    fatalError("failed to read file days");
  }


  uint32_t mins;
  if (util_recvU32(main_socketFd, &mins) != 0) {
    fatalError("failed to read file mins");
  }

  uint32_t ticks;
  if (util_recvU32(main_socketFd, &ticks) != 0) {
    fatalError("failed to read file ticks");
  }

  uint32_t commentLength;
  if (util_recvU32(main_socketFd, &commentLength) != 0) {
    fatalError("failed to read comment length");
  }

  char* comment;
  if (commentLength > 0) {
    comment = util_recvLatin1AsUtf8(main_socketFd, commentLength);
  } else {
    comment = 0;
  }

  dir_pushDirEntry(entryList, buffer, type, size, prot, days, mins, ticks, comment);

  return 1;
}


dir_entry_list_t*
dir_read(const char* command)
{
  if (util_sendCommand(main_socketFd, SQUIRT_COMMAND_DIR) != 0) {
    fatalError("failed to connect to squirtd server %d", main_socketFd);
  }

  if (util_sendLengthAndUtf8StringAsLatin1(main_socketFd, command) != 0) {
    fatalError("send() command failed");
  }

  uint32_t more;
  dir_entry_list_t *entryList = dir_newEntryList();
  do {
    more = dir_getDirEntry(entryList);
  } while (more);

  uint32_t error;

  if (util_recvU32(main_socketFd, &error) != 0) {
    fatalError("dir: failed to read remote status: %s", command);
  }

  if (error != 0) {
    dir_freeEntryList(entryList);
    entryList = 0;
  }

  dir_cleanup();

  return entryList;
}


int
dir_process(const char* command, void(*process)(dir_entry_list_t*))
{
  int error = 0;
  dir_entry_list_t *entryList = dir_read(command);

  if (entryList == 0) {
    error = -1;
  } else {
    if (process) {
      process(entryList);
    }

    dir_freeEntryList(entryList);
  }
  return error;
}


void
dir_main(int argc, char* argv[])
{
  if (argc != 3) {
    fatalError("incorrect number of arguments\nusage: %s hostname dir_name", main_argv0);
  }

  util_connect(argv[1]);
  if (dir_process(argv[2], squirt_dirPrintEntryList) != 0) {
    fatalError("unable to read %s", argv[2]);
  }
}
