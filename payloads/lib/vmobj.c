#include <sys/types.h>
#include <stdlib.h>

#include <ps5/kernel.h>

#include "vmobj.h"

#define VME_NEXT 0x08
#define VME_START 0x20
#define VME_END 0x28
#define VME_OBJECT 0x50
#define MAX_RANGES 65536

static struct vm_obj_range *g_r;
static int g_n;

int vmobj_load(pid_t pid) {
  uint64_t proc = kernel_get_proc(pid);
  uint64_t head = kernel_getlong(proc + KERNEL_OFFSET_PROC_P_VMSPACE);
  if (!head)
    return -1;
  free(g_r);
  g_r = calloc(MAX_RANGES, sizeof(*g_r));
  g_n = 0;
  for (uint64_t e = kernel_getlong(head + VME_NEXT); e && e != head && g_n < MAX_RANGES;
       e = kernel_getlong(e + VME_NEXT)) {
    uint8_t b[0x58];
    if (kernel_copyout(e, b, sizeof(b)))
      return -1;
    g_r[g_n++] = (struct vm_obj_range){*(uint64_t *)(b + VME_START),
                                       *(uint64_t *)(b + VME_END),
                                       *(uint64_t *)(b + VME_OBJECT)};
  }
  return g_n;
}

uint64_t vmobj_of(uint64_t addr) {
  int lo = 0, hi = g_n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (g_r[mid].end <= addr)
      lo = mid + 1;
    else if (g_r[mid].start > addr)
      hi = mid - 1;
    else
      return g_r[mid].obj;
  }
  return 0;
}
