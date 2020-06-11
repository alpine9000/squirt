#pragma once

char*
backup_loadSkipFile(const char* filename, int ignoreErrors);

void
backup_main(int argc, char* argv[]);

void
backup_cleanup();
