#pragma once

void
restore_printProgress(const char* filename, struct timeval* start, uint32_t total, uint32_t fileLength);
void
restore_main(int argc, char* argv[]);

void
restore_cleanup(void);
