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
listCli(void)
{
  for (int i = 0; i < (int)MaxCli(); i++) {
    struct Process * proc =  FindCliProc(i);
    if (proc) {
      Printf((APTR)"[%ld] %s (%s)\n", i, (int)proc->pr_Task.tc_Node.ln_Name, (int)(char*)commandName(proc));
    }
  }
}


int
main(int argc, char **argv)
{
  (void)argc,(void)argv;
  Forbid();
  listCli();
  Permit();
  return 0;
}
