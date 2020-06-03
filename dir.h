#pragma once
#include <stdint.h>

typedef struct direntry {
  const char* name;
  int32_t type;
  uint32_t size;
  uint32_t prot;
  uint32_t days;
  uint32_t mins;
  uint32_t ticks;
  const char* comment;
  struct direntry* next;
  int renderedSizeLength;
} dir_entry_t;

typedef struct dir_entry_list {
  dir_entry_t* head;
  dir_entry_t* tail;
  struct dir_entry_list *next;
  struct dir_entry_list *prev;
} dir_entry_list_t;


void
dir_cleanup(void);

void
dir_freeEntryLists(void);

void
dir_freeEntryList(dir_entry_list_t* list);

void
dir_freeEntry(dir_entry_t* ptr);

dir_entry_t*
dir_newDirEntry(void);

dir_entry_list_t*
dir_read(const char* hostname, const char* command);

int
dir_process(const char* hostname, const char* command, void(*process)(const char* hostname, dir_entry_list_t*));

void
dir_main(int argc, char* argv[]);
