#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"

#include <readline/readline.h>
#include <readline/history.h>

static char* srl_line_read = 0;
static const char* (*_srl_prompt)(void);
static void (*_srl_complete_hook)(const char* text);
static char* (*_srl_generator)(int* list_index, const char* text, int len);

static char *
srl_generator(const char *text, int state)
{
  static int list_index, len;

  if (!state) {
    list_index = 0;
    len = strlen(text);
  }

  if (text && text[0] == '!') {
    char* real = rl_filename_completion_function(&text[1], state);

    if (real) {
      if (util_isDirectory(real)) {
	rl_completion_append_character = '/';
      } else {
	rl_completion_append_character = ' ';
      }
      char* fake = malloc(strlen(real)+2);
      sprintf(fake, "!%s", real);
      free(real);
      return fake;
    } else {
      return 0;
    }
  }

  if (_srl_generator) {
    return _srl_generator(&list_index, text, len);
  } else {
    return 0;
  }
}


char *
srl_gets(void)
{
  if (srl_line_read) {
    free(srl_line_read);
    srl_line_read = (char *)NULL;
  }


  srl_line_read = readline(_srl_prompt ? _srl_prompt() :  "");

  if (srl_line_read && *srl_line_read) {
    add_history (srl_line_read);
  }

  return (srl_line_read);
}


static char*
srl_escape_spaces(const char* str)
{
  if (!str) {
    return 0;
  }
  int numSpaces = 0;
  int length = strlen(str);
  for (int i = 0; i < length; i++) {
    if (str[i] == ' ') {
      numSpaces++;
    }
  }
  char* ptr = malloc(strlen(str)+numSpaces+1);
  int j = 0;
  for (unsigned int i = 0; i < strlen(str); i++) {
    if (str[i] == ' ') {
      ptr[j++] ='\\';
    }
    ptr[j++] = str[i];
  }
  ptr[j] = 0;
  return ptr;
}


static char *
srl_dequote_func(char * text, int state)
{
  (void)state;
  char* ptr = strdup(text);
  char* ret = ptr;

  for (int i = 0; *text; i++) {
    (void)i;
    if (*text != '\\') {
      *ptr++ = *text;
    }
    text++;
  }
  *ptr = 0;

  return ret;
}


static int
srl_char_is_quoted(char* text, int index)
{
  int isquoted = 0;

  if (index > 0) {
    isquoted = text[index-1] == '\\' || text[index] == '\\';
  }

  return isquoted;
}


static char *
srl_quote_func(char * text, int state, char* blah)
{
  (void)state,(void)blah;
  return srl_escape_spaces(text);
}


static int
srl_directory_rewrite(char** name)
{
  char* backup = *name;
  *name = srl_dequote_func(*name, 0);
  if (backup) {
    free(backup);
  }
  return 1;
}


void
srl_write_history(void)
{
  write_history(util_getHistoryFile());
}


void
srl_cleanup(void)
{
  if (srl_line_read) {
    free(srl_line_read);
    srl_line_read = 0;
  }
}


static char **
srl_completion_function(const char *text, int start, int end)
{
  (void)start,(void)end;

  if (_srl_complete_hook) {
    _srl_complete_hook(text);
  }

  return rl_completion_matches(text, srl_generator);
}


void
srl_init(const char*(*prompt)(void),void (*complete_hook)(const char* text), char* (*generator)(int* list_index, const char* text, int len))
{
  using_history();
  read_history(util_getHistoryFile());

  _srl_prompt = prompt;
  _srl_generator = generator;
  _srl_complete_hook = complete_hook;

  rl_attempted_completion_function = srl_completion_function;

#ifdef _WIN32
    if (1) {
      rl_completer_quote_characters = "\"";
      rl_filename_quote_characters  = " `'=[]{}()<>|&\\\t" ;
    } else
#endif
    {
      rl_completer_quote_characters = "\\";
      rl_filename_quote_characters  = " `'=[]{}()<>|&\\\t" ;
      rl_filename_dequoting_function = srl_dequote_func;
      rl_filename_quoting_function = srl_quote_func;
      rl_char_is_quoted_p = srl_char_is_quoted;
      rl_directory_rewrite_hook = srl_directory_rewrite;
    }
}
