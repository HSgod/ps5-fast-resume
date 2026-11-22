#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "debug.h"
#include "proc.h"

#define PROC_P_FD 0x48
#define FDE_SIZE 0x30
#define DTYPE_KQUEUE 5

#define KQ_HEAD 0x38
#define KQ_COUNT 0x48
#define KQ_KNLISTSIZE 0xc0
#define KQ_KNLIST 0xc8
#define KQ_KNHASHMASK 0xd0
#define KQ_KNHASH 0xd8

#define KN_LINK 0x00
#define KN_TQE 0x18
#define KN_KEVENT 0x30
#define KN_STATUS 0x50
#define KN_SFFLAGS 0x54
#define KN_SDATA 0x58

static void print_knote(const char *tag, uint64_t kn) {
  uint8_t k[0x80];
  if (kernel_copyout(kn, k, sizeof(k))) {
    printf("    %s 0x%lx unreadable\n", tag, kn);
    return;
  }
  printf("    %s 0x%lx ident 0x%lx filter %d flags 0x%x fflags 0x%x data 0x%lx "
         "udata 0x%lx status 0x%x sfflags 0x%x sdata 0x%lx\n",
         tag, kn, *(uint64_t *)(k + KN_KEVENT), *(int16_t *)(k + KN_KEVENT + 8),
         *(uint16_t *)(k + KN_KEVENT + 10), *(uint32_t *)(k + KN_KEVENT + 12),
         *(uint64_t *)(k + KN_KEVENT + 16), *(uint64_t *)(k + KN_KEVENT + 24),
         *(uint32_t *)(k + KN_STATUS), *(uint32_t *)(k + KN_SFFLAGS),
         *(uint64_t *)(k + KN_SDATA));
}

static void walk_slist(const char *tag, uint64_t head) {
  for (uint64_t kn = kernel_getlong(head), n = 0; kn && n < 256;
       kn = kernel_getlong(kn + KN_LINK), n++)
    print_knote(tag, kn);
}

static void dump_kq(int fd, uint64_t kq) {
  int count = kernel_getint(kq + KQ_COUNT);
  int knlistsize = kernel_getint(kq + KQ_KNLISTSIZE);
  uint64_t knlist = kernel_getlong(kq + KQ_KNLIST);
  uint64_t hashmask = kernel_getlong(kq + KQ_KNHASHMASK);
  uint64_t knhash = kernel_getlong(kq + KQ_KNHASH);
  printf("kqueue fd %d 0x%lx count %d knlistsize %d hashmask 0x%lx\n", fd, kq,
         count, knlistsize, hashmask);
  for (uint64_t kn = kernel_getlong(kq + KQ_HEAD), n = 0; kn && n < 256;
       kn = kernel_getlong(kn + KN_TQE), n++)
    print_knote("pending", kn);
  for (int i = 0; knlist && i < knlistsize && i < 1024; i++)
    walk_slist("fd-reg ", knlist + 8 * i);
  for (uint64_t i = 0; knhash && i <= hashmask && i < 1024; i++)
    walk_slist("hash-reg", knhash + 8 * i);
}

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0)
    return 1;
  if (debug_attach(pid))
    return 1;
  usleep(100000);
  uint64_t proc = kernel_get_proc(pid);
  uint64_t tbl = kernel_getlong(kernel_getlong(proc + PROC_P_FD));
  int nfiles = kernel_getint(tbl);
  for (int fd = 0; fd < nfiles && fd < 4096; fd++) {
    uint64_t fp = kernel_getlong(tbl + 8 + FDE_SIZE * fd);
    if (fp && kernel_getshort(fp + 0x20) == DTYPE_KQUEUE)
      dump_kq(fd, kernel_getlong(fp));
  }
  debug_detach();
  return 0;
}
