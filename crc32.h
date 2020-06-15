#pragma once
#include <stdint.h>

int
crc32_sum(const char* filename, uint32_t *outCrc);

int
chsum32_sum(const char* filename, uint32_t *outCrc);
