#pragma once

#include <stdint.h>

char*
backup_loadSkipFile(const char* filename, int ignoreErrors);

void
backup_main(int argc, char* argv[]);

uint32_t
backup_doCrcVerify(const char* path);

void
backup_cleanup(void);
