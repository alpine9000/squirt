#pragma once

int
exec_cmd(int argc, char** argv);

int
exec_captureCmd(char** outputPtr, int argc, char** argv);

void
exec_cleanup(void);

void
exec_main(int argc, char* argv[]);
