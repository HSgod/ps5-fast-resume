#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ptrace.h>
#include <machine/reg.h>

#include "debug.h"
#include "proc.h"

static const char *stat_name(int s) {
  static const char *n[] = {"?", "IDL", "RUN", "SLEEP", "STOP", "ZOMB", "WAIT", "LOCK"};
  return s >= 0 && s < 8 ? n[s] : "?";
}

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0) {
    printf("no game process found\n");
    return 1;
  }
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD, pid};
  size_t size;
  debug_elevate();
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf) {
    printf("sysctl: %s, falling back to ptrace\n", strerror(errno));
    if (debug_attach(pid))
      return 1;
    int n = ptrace(PT_GETNUMLWPS, pid, NULL, 0);
    lwpid_t *l = calloc(n > 0 ? n : 1, sizeof(*l));
    n = ptrace(PT_GETLWPLIST, pid, (caddr_t)l, n);
    for (int i = 0; i < n; i++) {
      struct ptrace_lwpinfo li = {0};
      struct reg r = {0};
      ptrace(PT_LWPINFO, l[i], (caddr_t)&li, sizeof(li));
      ptrace(PT_GETREGS, l[i], (caddr_t)&r, 0);
      printf("%7d %-20s rip 0x%012lx rsp 0x%012lx rax 0x%lx rdi 0x%lx rsi 0x%lx "
             "rdx 0x%lx r8 0x%lx r9 0x%lx\n",
             l[i], li.pl_tdname, r.r_rip, r.r_rsp, r.r_rax, r.r_rdi, r.r_rsi,
             r.r_rdx, r.r_r8, r.r_r9);
    }
    debug_detach();
    return 0;
  }
  printf("pid %d title %s\n", pid, title);
  printf("%7s %-20s %-6s %-10s %10s %10s %4s\n", "TID", "NAME", "STATE",
         "WAIT", "UTIME_MS", "STIME_MS", "CPU");
  for (char *p = buf; p < buf + size;) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if (ki->ki_structsize <= 0)
      break;
    p += ki->ki_structsize;
    printf("%7d %-20s %-6s %-10s %10ld %10ld %4d\n", ki->ki_tid, ki->ki_tdname,
           stat_name(ki->ki_stat), ki->ki_wmesg[0] ? ki->ki_wmesg : "-",
           ki->ki_rusage.ru_utime.tv_sec * 1000 + ki->ki_rusage.ru_utime.tv_usec / 1000,
           ki->ki_rusage.ru_stime.tv_sec * 1000 + ki->ki_rusage.ru_stime.tv_usec / 1000,
           ki->ki_lastcpu);
  }
  free(buf);
  debug_restore_creds();
  return 0;
}
