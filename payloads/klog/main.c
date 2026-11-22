#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

#define TAIL (1024 * 1024)

int main(void) {
  debug_elevate();
  size_t size = 0;
  if (!sysctlbyname("kern.msgbuf", NULL, &size, NULL, 0) && size) {
    char *buf = malloc(size + 1);
    if (!sysctlbyname("kern.msgbuf", buf, &size, NULL, 0)) {
      buf[size] = 0;
      size_t off = size > TAIL ? size - TAIL : 0;
      printf("kern.msgbuf %zu bytes, last %zu:\n", size, size - off);
      fwrite(buf + off, 1, size - off, stdout);
      free(buf);
      debug_restore_creds();
      return 0;
    }
    free(buf);
  }
  printf("kern.msgbuf: %s, trying /dev/klog\n", strerror(errno));
  int fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    printf("/dev/klog: %s\n", strerror(errno));
    debug_restore_creds();
    return 1;
  }
  char chunk[4096];
  ssize_t n;
  size_t total = 0;
  while ((n = read(fd, chunk, sizeof(chunk))) > 0 && total < 4 * 1024 * 1024) {
    fwrite(chunk, 1, n, stdout);
    total += n;
  }
  printf("\n/dev/klog read %zu bytes (%s)\n", total, n < 0 ? strerror(errno) : "eof");
  close(fd);
  debug_restore_creds();
  return 0;
}
