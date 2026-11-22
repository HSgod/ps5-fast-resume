#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ps5/kernel.h>

#include "proc.h"

#define MiB(x) ((double)(x) / (1024.0 * 1024.0))
#define PG ((uint64_t)PAGE_SIZE)

static const char *type_name(int t) {
  static char tmp[16];
  switch (t) {
  case KVME_TYPE_NONE: return "none";
  case KVME_TYPE_DEFAULT: return "default";
  case KVME_TYPE_VNODE: return "vnode";
  case KVME_TYPE_SWAP: return "swap";
  case KVME_TYPE_DEVICE: return "device";
  case KVME_TYPE_PHYS: return "phys";
  case KVME_TYPE_DEAD: return "dead";
  case KVME_TYPE_SG: return "sg";
  case KVME_TYPE_MGTDEVICE: return "mgtdev";
  case KVME_TYPE_UNKNOWN: return "unknown";
  }
  snprintf(tmp, sizeof(tmp), "t%d", t);
  return tmp;
}

static void hexdump(const void *p, size_t n) {
  const unsigned char *b = p;
  for (size_t i = 0; i < n; i += 16) {
    printf("  %04zx:", i);
    for (size_t j = i; j < i + 16 && j < n; j++)
      printf(" %02x", b[j]);
    printf("  ");
    for (size_t j = i; j < i + 16 && j < n; j++)
      putchar(isprint(b[j]) ? b[j] : '.');
    putchar('\n');
  }
}

#define NTYPES 32

int main(void) {
  char title[10] = {0};
  pid_t pid = find_game(title, 1);
  if (pid < 0) {
    printf("no game process found\n");
    return 1;
  }
  printf("game pid %d title %s kproc 0x%lx\n", pid, title,
         (unsigned long)kernel_get_proc(pid));

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
  size_t size;
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf) {
    printf("KERN_PROC_VMMAP failed: errno %d (%s)\n", errno, strerror(errno));
    return 1;
  }
  printf("vmmap bytes %zu, sizeof(kinfo_vmentry) %zu\n", size,
         sizeof(struct kinfo_vmentry));
  if (size >= 16) {
    int first = *(int *)buf;
    printf("first record raw (%d bytes, showing up to 256):\n", first);
    hexdump(buf, first > 0 && first < 256 ? (size_t)first : 256);
  }

  uint64_t tot_virt = 0, tot_res = 0, tot_priv = 0;
  uint64_t t_virt[NTYPES] = {0}, t_res[NTYPES] = {0}, t_n[NTYPES] = {0};
  uint64_t w_virt = 0, w_res = 0;
  int n = 0;

  printf("\n%-4s %-18s %-18s %10s %9s %9s %5s %8s %6s %4s  %s\n", "#", "START",
         "END", "SIZE_KiB", "RES_KiB", "PRIV_KiB", "PROT", "TYPE", "FLAGS",
         "REC", "PATH");
  for (char *p = buf; p < buf + size; n++) {
    struct kinfo_vmentry *e = (struct kinfo_vmentry *)p;
    if (e->kve_structsize <= 0)
      break;
    p += e->kve_structsize;

    uint64_t len = e->kve_end - e->kve_start;
    uint64_t res = (uint64_t)e->kve_resident * PG;
    uint64_t priv = (uint64_t)e->kve_private_resident * PG;
    int ti = e->kve_type >= 0 && e->kve_type < NTYPES ? e->kve_type : NTYPES - 1;

    tot_virt += len;
    tot_res += res;
    tot_priv += priv;
    t_virt[ti] += len;
    t_res[ti] += res;
    t_n[ti]++;
    if (e->kve_protection & KVME_PROT_WRITE) {
      w_virt += len;
      w_res += res;
    }

    printf("%-4d 0x%016lx 0x%016lx %10lu %9lu %9lu  %c%c%c%x %8s %6x %4d  %s\n",
           n, e->kve_start, e->kve_end, len >> 10, res >> 10, priv >> 10,
           e->kve_protection & KVME_PROT_READ ? 'r' : '-',
           e->kve_protection & KVME_PROT_WRITE ? 'w' : '-',
           e->kve_protection & KVME_PROT_EXEC ? 'x' : '-',
           e->kve_protection >> 3, type_name(e->kve_type), e->kve_flags,
           e->kve_structsize, e->kve_path);
  }

  printf("\nentries %d\n", n);
  printf("virtual  %10.1f MiB\n", MiB(tot_virt));
  printf("resident %10.1f MiB (page size assumed 0x%lx)\n", MiB(tot_res), PG);
  printf("private  %10.1f MiB\n", MiB(tot_priv));
  printf("writable virtual %10.1f MiB, resident %10.1f MiB\n", MiB(w_virt),
         MiB(w_res));
  printf("\nby type:\n");
  for (int i = 0; i < NTYPES; i++)
    if (t_n[i])
      printf("  %-8s n=%-5lu virt %10.1f MiB  res %10.1f MiB\n", type_name(i),
             t_n[i], MiB(t_virt[i]), MiB(t_res[i]));

  free(buf);
  return 0;
}
