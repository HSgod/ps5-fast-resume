#include <sys/types.h>
#include <sys/ptrace.h>
#include <machine/reg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ps5/kernel.h>

#include "debug.h"
#include "proc.h"

#ifndef SPIN_THREAD
#define SPIN_THREAD "eboot.bin"
#endif

static void hex(pid_t pid, const char *tag, uint64_t addr, size_t len) {
  unsigned char b[1024];
  if (len > sizeof(b))
    len = sizeof(b);
  if (kernel_proc_copyout(pid, addr, b, len)) {
    printf("%s 0x%lx: unreadable\n", tag, addr);
    return;
  }
  printf("%s 0x%lx %zu\n", tag, addr, len);
  for (size_t i = 0; i < len; i += 16) {
    printf("  %012lx:", addr + i);
    for (size_t j = i; j < i + 16; j++)
      printf(" %02x", b[j]);
    printf("\n");
  }
}

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0 || debug_attach(pid))
    return 1;
  int n = ptrace(PT_GETNUMLWPS, pid, NULL, 0);
  lwpid_t *l = calloc(n > 0 ? n : 1, sizeof(*l));
  n = ptrace(PT_GETLWPLIST, pid, (caddr_t)l, n);
  struct reg r = {0};
  int sel = 0;
  for (int i = 0; i < n; i++) {
    struct ptrace_lwpinfo li = {0};
    ptrace(PT_LWPINFO, l[i], (caddr_t)&li, sizeof(li));
    if (!strncmp(li.pl_tdname, SPIN_THREAD, sizeof(SPIN_THREAD) - 1))
      sel = i;
  }
  ptrace(PT_GETREGS, l[sel], (caddr_t)&r, 0);
  printf("lwp %d\n", l[sel]);
  printf("rip %lx rsp %lx rbp %lx rflags %lx\n", r.r_rip, r.r_rsp, r.r_rbp, r.r_rflags);
  printf("rax %lx rbx %lx rcx %lx rdx %lx\n", r.r_rax, r.r_rbx, r.r_rcx, r.r_rdx);
  printf("rsi %lx rdi %lx r8 %lx r9 %lx\n", r.r_rsi, r.r_rdi, r.r_r8, r.r_r9);
  printf("r10 %lx r11 %lx r12 %lx r13 %lx r14 %lx r15 %lx\n", r.r_r10, r.r_r11,
         r.r_r12, r.r_r13, r.r_r14, r.r_r15);
  hex(pid, "code", (r.r_rip & ~0xfUL) - 0x80, 0x100);
  hex(pid, "stack", r.r_rsp, 0x200);
  uint64_t regs[] = {r.r_rax, r.r_rbx, r.r_rcx, r.r_rdx, r.r_rsi, r.r_rdi,
                     r.r_r8, r.r_r9, r.r_r12, r.r_r13, r.r_r14, r.r_r15};
  const char *names[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi",
                         "r8", "r9", "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 12; i++)
    if (regs[i] > 0x10000 && regs[i] < 0x10000000000UL)
      hex(pid, names[i], regs[i] & ~0xfUL, 0x40);
  uint64_t arr = 0;
  int32_t cnt = 0;
  kernel_proc_copyout(pid, r.r_rbp - 0x70, &arr, 8);
  kernel_proc_copyout(pid, r.r_rbp - 0x68, &cnt, 4);
  if (!arr)
    arr = r.r_rbp - 0x90;
  printf("wait array 0x%lx count %d\n", arr, cnt);
  for (int i = 0; i < cnt && i < 16; i++) {
    uint64_t obj = 0;
    kernel_proc_copyout(pid, arr + 8 * i, &obj, 8);
    char tag[32];
    snprintf(tag, sizeof(tag), "task[%d]", i);
    hex(pid, tag, obj, 0xc0);
    uint64_t vt = 0;
    kernel_proc_copyout(pid, obj, &vt, 8);
    hex(pid, "  vtable", vt, 0x40);
  }
  free(l);
  return 0;
}
