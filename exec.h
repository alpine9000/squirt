#pragma once

int
exec_cmd(const char* hostname, int argc, char** argv);

void
exec_cleanup(void);

void
exec_main(int argc, char* argv[]);
