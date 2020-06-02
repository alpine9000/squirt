#pragma once

void
squirt_cleanup(void);

int
squirt_file(const char* hostname, const char* filename, const char* destFilename, int writeToCurrentDir, int progress);

int
squirt_main(int argc, char* argv[]);
