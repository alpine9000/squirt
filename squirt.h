#pragma once
#include <stdint.h>

void
squirt_cleanup(void);

int
squirt_file(const char* filename, const char* progressHeader, const char* destFilename, int writeToCurrentDir, void (*progress)(const char* progressHeader, struct timeval* start, uint32_t total, uint32_t fileLength));

void
squirt_main(int argc, char* argv[]);
