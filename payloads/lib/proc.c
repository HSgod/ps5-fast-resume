#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"

int sceKernelGetAppInfo(pid_t pid, void *info);

void *sysctl_alloc(int *mib, unsigned n, size_t *size) {
  void *buf = NULL;
  for (int tries = 0; tries < 4; tries++) {
    *size = 0;
    if (sysctl(mib, n, NULL, size, NULL, 0))
      break;
    *size += *size / 2 + 0x10000;
    free(buf);
    if (!(buf = malloc(*size)))
      return NULL;
    if (!sysctl(mib, n, buf, size, NULL, 0))
      return buf;
    if (errno != ENOMEM)
      break;
  }
  free(buf);
  return NULL;
}

int title_of(pid_t pid, char out[10]) {
  unsigned char raw[0x100];
  memset(raw, 0, sizeof(raw));
  out[0] = 0;
  if (sceKernelGetAppInfo(pid, raw))
    return -1;
  for (int i = 0; i + 9 <= (int)sizeof(raw); i++) {
    int ok = 1;
    for (int j = 0; j < 4 && ok; j++)
      ok = isupper(raw[i + j]);
    for (int j = 4; j < 9 && ok; j++)
      ok = isdigit(raw[i + j]);
    if (ok) {
      memcpy(out, raw + i, 9);
      out[9] = 0;
      return i;
    }
  }
  return -1;
}

pid_t find_game(char title[10], int verbose) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t size;
  pid_t found = -1;
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf)
    return -1;
  for (char *p = buf; p < buf + size;) {
    struct kinfo_proc *ki = (struct kinfo_proc *)p;
    if (ki->ki_structsize <= 0)
      break;
    p += ki->ki_structsize;
    char t[10];
    if (title_of(ki->ki_pid, t) < 0 || !strncmp(t, "NPXS", 4))
      continue;
    if (verbose)
      printf("candidate pid %d title %s comm %s\n", ki->ki_pid, t, ki->ki_comm);
    if (found < 0) {
      found = ki->ki_pid;
      memcpy(title, t, 10);
    }
  }
  free(buf);
  return found;
}
