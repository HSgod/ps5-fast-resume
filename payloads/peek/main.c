#include <sys/types.h>
#include <stdio.h>

#include <ps5/kernel.h>

#include "proc.h"

static const unsigned long addrs[] = {0x800000380, 0x8000007a0, 0x8000010e0,
                                      0x800001440, 0x800001b20, 0x800002240,
                                      0x800001c80, 0x800000980};

int main(void) {
  char title[10];
  pid_t pid = find_game(title, 0);
  if (pid < 0)
    return 1;
  for (unsigned i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
    unsigned char b[32];
    if (kernel_proc_copyout(pid, addrs[i] - 0x10, b, sizeof(b))) {
      printf("%lx unreadable\n", addrs[i]);
      continue;
    }
    printf("%lx:", addrs[i] - 0x10);
    for (unsigned j = 0; j < sizeof(b); j++)
      printf(" %02x", b[j]);
    printf("\n");
  }
  return 0;
}
