#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "proc.h"

#define DEBUGGER_AUTHID 0x4800000000010003UL

int main(void) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t size;
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf)
    return 1;
  for (char *p = buf; p < buf + size;) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if (ki->ki_structsize <= 0)
      break;
    p += ki->ki_structsize;
    if (ki->ki_pid == getpid() || strcmp(ki->ki_comm, "payload.elf"))
      continue;
    if (kernel_get_ucred_authid(ki->ki_pid) != DEBUGGER_AUTHID)
      continue;
    int r = kill(ki->ki_pid, SIGKILL);
    printf("kill %d: %s\n", ki->ki_pid, r ? strerror(errno) : "ok");
  }
  free(buf);
  return 0;
}
