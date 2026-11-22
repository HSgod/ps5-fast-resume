#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

int main(void) {
  signal(SIGPIPE, SIG_IGN);
  debug_elevate();
  int fd = open("/dev/klog", O_RDONLY);
  if (fd < 0) {
    printf("/dev/klog: %s\n", strerror(errno));
    return 1;
  }
  printf("klog stream started\n");
  fflush(stdout);
  char buf[4096];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    if (fwrite(buf, 1, n, stdout) != (size_t)n)
      break;
    fflush(stdout);
  }
  close(fd);
  return 0;
}
