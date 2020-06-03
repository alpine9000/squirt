#pragma once

void
srl_init(const char*(*_srl_prompt)(void),void (*complete_hook)(const char* text), char* (*generator)(int* list_index, const char* text, int len));

void
srl_write_history(void);

void
srl_cleanup(void);

char *
srl_gets(void);
