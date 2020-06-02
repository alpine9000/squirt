/* Create and destroy argument vectors (argv's)
   Copyright (C) 1992, 2001 Free Software Foundation, Inc.
   Written by Fred Fish @ Cygnus Support

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef _WIN32
#include <alloca.h>
#endif
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "argv.h"

#define INITIAL_MAXARGC 8
#define MAX_ARGC 32768

#ifndef EOS
#define EOS '\0'
#endif

 char *
 strdup(const char *s1);

void argv_free (vector)
     char **vector;
{
  register char **scan;

  if (vector != NULL)
    {
      for (scan = vector; *scan != NULL; scan++)
	{
	  free (*scan);
	}
      free (vector);
    }
}

#ifdef isspace
#undef isspace
#endif
#define isspace(ch) ((ch) == ' ' || (ch) == '\t')

char **argv_build (char* input)
{
  char *arg;
  char *copybuf;
  int squote = 0;
  int dquote = 0;
  int bsquote = 0;
  int argc = 0;
  int maxargc = 0;
  char **argv = NULL;
  char **nargv;

  if (input != NULL)
    {
      copybuf = (char *) alloca (strlen (input) + 1);
      /* Is a do{}while to always execute the loop once.  Always return an
	 argv, even for null strings.  See NOTES above, test case below. */
      do
	{
	  /* Pick off argv[argc] */
	  while (isspace (*input))
	    {
	      input++;
	    }
	  if ((maxargc == 0) || (argc >= (maxargc - 1)))
	    {
	      /* argv needs initialization, or expansion */
	      if (argv == NULL)
		{
		  maxargc = INITIAL_MAXARGC;
		  nargv = (char **) malloc (maxargc * sizeof (char *));
		}
	      else
		{
		  maxargc *= 2;
		  nargv = (char **) realloc (argv, maxargc * sizeof (char *));
		}
	      if (nargv == NULL)
		{
		  if (argv != NULL)
		    {
		      argv_free (argv);
		      argv = NULL;
		    }
		  break;
		}
	      argv = nargv;
	      argv[argc] = NULL;
	    }
	  /* Begin scanning arg */
	  arg = copybuf;
	  while (*input != EOS)
	    {
	      if (isspace (*input) && !squote && !dquote && !bsquote)
		{
		  break;
		}
	      else
		{
		  if (bsquote)
		    {
		      bsquote = 0;
		      *arg++ = *input;
		    }
		  else if (*input == '\\')
		    {
		      bsquote = 1;
#ifdef ARGV_KEEP_QUOTES
		      *arg++ = *input;
#endif
		    }
		  else if (squote)
		    {
		      if (*input == '\'')
			{
			  squote = 0;
			}
		      else
		      	{
			  *arg++ = *input;
			}
		    }
		  else if (dquote)
		    {
		      if (*input == '"')
			{
			  dquote = 0;
			}
#ifdef ARGV_KEEP_QUOTES
		      *arg++ = *input;
#else
		      else
			{
			  *arg++ = *input;
			}
#endif
		    }
		  else
		    {
		      if (*input == '\'')
			{
			  squote = 1;
			}
		      else if (*input == '"')
			{
			  dquote = 1;
			}
		      else
			{
			  *arg++ = *input;
			}
		    }
		  input++;
		}
	    }
	  *arg = EOS;
	  argv[argc] = strdup (copybuf);
	  if (argv[argc] == NULL)
	    {
	      argv_free (argv);
	      argv = NULL;
	      break;
	    }
	  argc++;
	  argv[argc] = NULL;

	  while (isspace (*input))
	    {
	      input++;
	    }
	}
      while (*input != EOS);
    }
  return (argv);
}

int argv_argc(char** vector)
{
  int argc = 0;
  if (vector != 0) {
    for (; argc < MAX_ARGC && vector[argc] != 0; argc++);
  }

  return argc;
}

char* argv_reconstruct(char** vector)
{
  int len = 0;
  if (vector == 0) {
    return 0;
  }

  for (int argc = 0; argc < 32768 && vector[argc] != 0; argc++) {
    len += (strlen(vector[argc])+1);
  }

  len++;

  char* cmd = (char*)malloc(len);

  cmd[0] = 0;

  for (int argc = 0; argc < MAX_ARGC && vector[argc] != 0; argc++) {
    strcat(cmd, vector[argc]);
    strcat(cmd, " ");
  }

  cmd[len-1] = 0;
  return cmd;
}
