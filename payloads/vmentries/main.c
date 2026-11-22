#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <ps5/kernel.h>

#include "proc.h"

#define ENTRY_SIZE 0x120
#define MAX_ENTRIES 20000

static void name_of(const uint8_t *e, char *out, size_t max) {
  out[0] = 0;
  for (size_t i = 0x60; i + 4 < ENTRY_SIZE; i++) {
    size_t n = 0;
    while (i + n < ENTRY_SIZE && isprint(e[i + n]) && n + 1 < max)
      n++;
    if (n >= 4 && (i + n == ENTRY_SIZE || e[i + n] == 0)) {
      memcpy(out, e + i, n);
      out[n] = 0;
      snprintf(out + n, max - n, "@%zx", i);
      return;
    }
  }
}

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0)
    return 1;
  uint64_t proc = kernel_get_proc(pid);
  uint64_t vms = kernel_getlong(proc + KERNEL_OFFSET_PROC_P_VMSPACE);
  printf("pid %d vmspace 0x%lx\n", pid, vms);
  uint64_t head = vms;
  uint64_t e = kernel_getlong(head + 0x08);
  int n = 0;
  while (e && e != head && n < MAX_ENTRIES) {
    uint8_t b[ENTRY_SIZE];
    if (kernel_copyout(e, b, sizeof(b)))
      break;
    char name[48];
    name_of(b, name, sizeof(name));
    printf("%lx %lx", *(uint64_t *)(b + 0x20), *(uint64_t *)(b + 0x28));
    for (int off = 0x30; off < 0x70; off += 8)
      printf(" %lx", *(uint64_t *)(b + off));
    printf(" | %s\n", name);
    if (n < 6) {
      for (int off = 0; off < ENTRY_SIZE; off += 16)
        printf("    +%03x %016lx %016lx\n", off, *(uint64_t *)(b + off),
               *(uint64_t *)(b + off + 8));
    }
    e = *(uint64_t *)(b + 0x08);
    n++;
  }
  printf("entries %d\n", n);
  return 0;
}
