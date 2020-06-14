#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/socket.h>

static const char*
commandName(struct Process *proc)
{
  struct CommandLineInterface* cli = BADDR(proc->pr_CLI);
  static char buffer[256];
  char* src = BADDR(cli->cli_CommandName);
  char* dest = buffer;
  for (; *src != 0 && dest < &buffer[sizeof(buffer)-2]; src++) {
    if (*src >=  '!') {
      *dest++ = *src;
    }
  }
  *dest = 0;
  return buffer;
}

static void
sendCtrlC(int cliNum)
{
  struct Process * proc =  FindCliProc(cliNum);
  Printf((APTR)"Sending Ctrl-C to %s (%s)\n", (int)proc->pr_Task.tc_Node.ln_Name, (int)(char*)commandName(proc));
  Signal((struct Task*)proc, SIGBREAKF_CTRL_C);
}

static int32_t
_atoi(const char *string)
{
  int result = 0;
  unsigned int digit;
  int sign;

  while (*string == ' ') {
    string += 1;
  }

  if (*string == '-') {
    sign = 1;
    string += 1;
  } else {
    sign = 0;
    if (*string == '+') {
      string += 1;
    }
  }

  for ( ; ; string += 1) {
    digit = *string - '0';
    if (digit > 9) {
      break;
    }
    result = (10*result) + digit;
  }

  if (sign) {
    return -result;
  }
  return result;
}

int
main(int argc, char **argv)
{
  int error = 0;

  if (argc != 2) {
    Printf((APTR)"usage: %s Cli#\n", (int)argv[0]);
    return 1;
  }

  Forbid();

  int cliNum = _atoi(argv[1]);
  if (cliNum < 1 || cliNum > (int)MaxCli()) {
    Printf((APTR)"%s: invalid Cli#\n", (int)argv[0]);
    error = 2;
    goto exit;
  } else {
    sendCtrlC(cliNum);
  }

  exit:
  Permit();
  return error;
}
