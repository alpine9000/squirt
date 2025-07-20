#pragma once

void
srl_init(const char*(*_srl_prompt)(void),void (*complete_hook)(const char* text, const char* full_command_line), char* (*generator)(int* list_index, const char* text, int len));

char* srl_escape_spaces(const char* str);

void
srl_write_history(void);

void
srl_cleanup(void);

char *
srl_gets(void);
