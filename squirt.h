#pragma once
#include <stdint.h>

void
squirt_cleanup(void);

int
squirt_file(const char* filename, const char* destFilename, int writeToCurrentDir, int progress);

void
squirt_main(int argc, char* argv[]);
