#pragma once
#include <stdint.h>

int32_t
squirt_suckFile(const char* filename, const char* progressHeader,  void (*progress)(const char* progressHeader, struct timeval* start, uint32_t total, uint32_t fileLength), const char* destFilename, uint32_t* protection);

void
suck_cleanup(void);

void
suck_main(int argc, char* argv[]);
