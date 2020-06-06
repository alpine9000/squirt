#pragma once

#include "dir.h"

#define SQUIRT_EXALL_INFO_DIR  ".__squirt"
#define SQUIRT_EXALL_INFO_DIR_NAME  SQUIRT_EXALL_INFO_DIR"/"

int
exall_readExAllData(dir_entry_t* entry, const char* path);

int
exall_identicalExAllData(dir_entry_t* one, dir_entry_t* two);

int
exall_saveExAllData(dir_entry_t* entry, const char* path);
