#pragma once

void
srl_init(const char*(*_srl_prompt)(void),void (*complete_hook)(const char* text, const char* full_command_line), char* (*generator)(int* list_index, const char* text, int len));

char*
srl_escapeSpaces(const char* str);

void
srl_writeHistory(void);

void
srl_cleanup(void);

char *
srl_gets(void);

extern int srl_insideQuotesFlag;
