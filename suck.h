#pragma once
#include <stdint.h>

int32_t
squirt_suckFile(const char* hostname, const char* filename, int progress, const char* destFilename, uint32_t* protection);

void
suck_cleanup(void);

void
suck_main(int argc, char* argv[]);
