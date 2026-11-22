#include <stdio.h>
#include <ps5/kernel.h>

int main(void) {
  printf("fast-resume hello, fw 0x%08x\n", kernel_get_fw_version());
  return 0;
}
