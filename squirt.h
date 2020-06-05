#pragma once
#include <stdint.h>

void
squirt_cleanup(void);

int
squirt_file(const char* hostname, const char* filename, const char* destFilename, uint32_t protection, int writeToCurrentDir, int progress);

void
squirt_main(int argc, char* argv[]);
