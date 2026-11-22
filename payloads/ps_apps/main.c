#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ps5/kernel.h>

int sceKernelGetAppInfo(pid_t pid, void *info);

static uint32_t app_info(pid_t pid, char title[10]) {
  unsigned char raw[0x100];
  memset(raw, 0, sizeof(raw));
  title[0] = 0;
  if (sceKernelGetAppInfo(pid, raw))
    return 0;
  for (int i = 0; i + 9 <= (int)sizeof(raw); i++) {
    int ok = 1;
    for (int j = 0; j < 4 && ok; j++)
      ok = isupper(raw[i + j]);
    for (int j = 4; j < 9 && ok; j++)
      ok = isdigit(raw[i + j]);
    if (ok) {
      memcpy(title, raw + i, 9);
      title[9] = 0;
      break;
    }
  }
  return *(uint32_t *)raw;
}

#define MiB(x) ((x) / (1024.0 * 1024.0))

int main(int argc, char **argv) {
#ifdef SHOW_ALL
  int all = 1;
#else
  int all = argc > 1 && !strcmp(argv[1], "-a");
#endif
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t size = 0;
  void *buf;

  if (sysctl(mib, 4, NULL, &size, NULL, 0)) {
    perror("sysctl");
    return 1;
  }
  size += size / 4;
  if (!(buf = malloc(size))) {
    perror("malloc");
    return 1;
  }
  if (sysctl(mib, 4, buf, &size, NULL, 0)) {
    perror("sysctl");
    free(buf);
    return 1;
  }

  printf("fw 0x%08x\n", kernel_get_fw_version());
  printf("%7s %7s %-9s %6s %8s %8s %18s %18s  %s\n", "PID", "PPID", "TITLE",
         "APPID", "RSS_MiB", "VSZ_MiB", "AUTHID", "KPROC", "COMM");

  for (char *p = buf; p < (char *)buf + size;) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    char title[10];

    if (ki->ki_structsize <= 0)
      break;
    p += ki->ki_structsize;

    uint32_t app_id = app_info(ki->ki_pid, title);

    if (!all && !title[0])
      continue;

    printf("%7d %7d %-9s %6x %8.1f %8.1f 0x%016lx 0x%016lx  %s\n", ki->ki_pid,
           ki->ki_ppid, title[0] ? title : "-", app_id,
           MiB((double)ki->ki_rssize * PAGE_SIZE), MiB((double)ki->ki_size),
           kernel_get_ucred_authid(ki->ki_pid),
           (unsigned long)kernel_get_proc(ki->ki_pid), ki->ki_comm);
  }

  free(buf);
  return 0;
}
