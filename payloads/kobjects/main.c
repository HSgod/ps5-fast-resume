#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ps5/kernel.h>

#include "debug.h"
#include "proc.h"

#define PROC_P_FD 0x48
#define FDT_ENTRIES 0x8
#define FDE_SIZE 0x30

static const char *kf_type(int t) {
  static const char *n[] = {"none", "vnode", "socket", "pipe", "fifo", "kqueue",
                            "crypto", "mqueue", "shm", "sem", "pts", "procdesc",
                            "dev"};
  static char tmp[16];
  if (t >= 0 && t <= 12)
    return n[t];
  snprintf(tmp, sizeof(tmp), "t%d", t);
  return tmp;
}

static void raw_files(pid_t pid) {
  uint64_t proc = kernel_get_proc(pid);
  uint64_t fdp = kernel_getlong(proc + PROC_P_FD);
  uint64_t tbl = kernel_getlong(fdp);
  int nfiles = (int)kernel_getint(tbl);
  printf("\nraw: proc 0x%lx filedesc 0x%lx table 0x%lx nfiles %d\n", proc, fdp,
         tbl, nfiles);
  printf("%4s %-18s %-18s %-18s %-18s %6s %6s %8s\n", "FD", "FILE", "F_DATA",
         "F_OPS", "F_VNODE", "TYPE", "FLAG", "COUNT");
  for (int fd = 0; fd < nfiles && fd < 4096; fd++) {
    uint64_t fp = kernel_getlong(tbl + FDT_ENTRIES + FDE_SIZE * fd);
    if (!fp)
      continue;
    uint8_t f[0x40];
    if (kernel_copyout(fp, f, sizeof(f)))
      continue;
    printf("%4d 0x%016lx 0x%016lx 0x%016lx 0x%016lx %6u %6x %8u\n", fd, fp,
           *(uint64_t *)(f + 0x00), *(uint64_t *)(f + 0x08),
           *(uint64_t *)(f + 0x18), *(uint16_t *)(f + 0x20),
           *(uint32_t *)(f + 0x24), *(uint32_t *)(f + 0x28));
  }
}

#define PROC_IDT 0xc48
#define IDT_NPAGES 0x220
#define IDT_PER_PAGE 128
#define IDE_SIZE 0x30
#define IDE_OBJ 0x10
#define IDE_TYPE 0x20
#define IDE_STATE 0x26
#define IDE_GEN 0x28
#define TYPE_EVF 0x110
#define TYPE_OSEM 0x120

static void sony_objects(pid_t pid) {
  uint64_t proc = kernel_get_proc(pid);
  uint64_t idt = kernel_getlong(proc + PROC_IDT);
  uint32_t npages = kernel_getint(idt + IDT_NPAGES);
  printf("\nsony object table 0x%lx pages %u\n", idt, npages);
  int counts[0x200] = {0};
  for (uint32_t pg = 0; pg < npages && pg < 64; pg++) {
    uint64_t page = kernel_getlong(idt + 8 * pg);
    if (!page)
      continue;
    uint8_t ents[IDT_PER_PAGE * IDE_SIZE];
    if (kernel_copyout(page, ents, sizeof(ents)))
      continue;
    for (int i = 0; i < IDT_PER_PAGE; i++) {
      uint8_t *e = ents + i * IDE_SIZE;
      uint16_t state = *(uint16_t *)(e + IDE_STATE);
      uint16_t type = *(uint16_t *)(e + IDE_TYPE);
      if (state != 3)
        continue;
      uint32_t idx = pg * IDT_PER_PAGE + i;
      uint32_t id = ((*(uint32_t *)(e + IDE_GEN) << 13) & 0xffff) | idx;
      uint64_t obj = *(uint64_t *)(e + IDE_OBJ);
      counts[type & 0x1ff]++;
      if (type == TYPE_EVF) {
        uint8_t o[0x58];
        kernel_copyout(obj, o, sizeof(o));
        printf("  evf  id %#06x obj 0x%lx pattern 0x%016lx attr %#x waiters %u\n",
               id, obj, *(uint64_t *)o, *(uint32_t *)(o + 0x48),
               *(uint32_t *)(o + 0x4c));
      } else if (type == TYPE_OSEM) {
        uint8_t o[0x60];
        kernel_copyout(obj, o, sizeof(o));
        printf("  osem id %#06x obj 0x%lx count %d init %d max %d attr %#x waiters %u\n",
               id, obj, *(int32_t *)(o + 0x40), *(int32_t *)(o + 0x48),
               *(int32_t *)(o + 0x4c), *(uint32_t *)(o + 0x44),
               *(uint32_t *)(o + 0x50));
      }
    }
  }
  printf("object types in use:");
  for (int t = 0; t < 0x200; t++)
    if (counts[t])
      printf(" %#x:%d", t, counts[t]);
  printf("\n");
}

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0) {
    printf("no game process found\n");
    return 1;
  }
  printf("pid %d title %s\n", pid, title);

  debug_elevate();
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, pid};
  size_t size;
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf) {
    printf("KERN_PROC_FILEDESC: %s\n", strerror(errno));
  } else {
    int first = *(int *)buf;
    printf("filedesc bytes %zu, first record %d bytes\n", size, first);
    printf("%4s %-7s %4s %6s %12s  %s\n", "FD", "TYPE", "REF", "FLAGS",
           "OFFSET", "PATH");
    for (char *p = buf; p < buf + size;) {
      struct kinfo_file *k = (struct kinfo_file *)p;
      if (k->kf_structsize <= 0)
        break;
      p += k->kf_structsize;
      printf("%4d %-7s %4d %6x %12ld  %s\n", k->kf_fd, kf_type(k->kf_type),
             k->kf_ref_count, k->kf_flags, (long)k->kf_offset, k->kf_path);
    }
    free(buf);
  }
  raw_files(pid);
  sony_objects(pid);
  debug_restore_creds();
  return 0;
}
