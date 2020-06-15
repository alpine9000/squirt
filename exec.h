#pragma once

int
exec_cmd(int argc, char** argv);

char*
exec_captureCmd(uint32_t* errorCode, int argc, char** argv);

void
exec_cleanup(void);

void
exec_main(int argc, char* argv[]);
