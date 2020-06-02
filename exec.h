#pragma once

int
exec_cmd(const char* hostname, int argc, char** argv);

void
exec_cleanup(void);

int
exec_main(int argc, char* argv[]);
