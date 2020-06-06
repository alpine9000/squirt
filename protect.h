#pragma once
#include <stdint.h>
#include "dir.h"

void
protect_cleanup(void);

int
protect_file(const char* filename, uint32_t protection, dir_datestamp_t* dateStamp);
