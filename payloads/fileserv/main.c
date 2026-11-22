#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 9031
#define BUFSZ (1UL << 20)

static int send_all(int s, const void *buf, size_t len) {
  const char *p = buf;
  while (len) {
    ssize_t n = send(s, p, len, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return -1;
    }
    p += n;
    len -= n;
  }
  return 0;
}

static int read_line(int s, char *buf, size_t max) {
  size_t i = 0;
  while (i + 1 < max) {
    char c;
    ssize_t n = recv(s, &c, 1, 0);
    if (n <= 0)
      return -1;
    if (c == '\n')
      break;
    buf[i++] = c;
  }
  buf[i] = 0;
  return 0;
}

static void do_get(int s, const char *path, char *buf, uint64_t limit) {
  uint64_t size = UINT64_MAX;
  struct stat st;
  int fd = open(path, O_RDONLY);
  if (fd < 0 || fstat(fd, &st)) {
    send_all(s, &size, sizeof(size));
    if (fd >= 0)
      close(fd);
    return;
  }
  size = (uint64_t)st.st_size < limit ? (uint64_t)st.st_size : limit;
  if (send_all(s, &size, sizeof(size)) == 0) {
    uint64_t left = size;
    while (left) {
      ssize_t n = read(fd, buf, left < BUFSZ ? left : BUFSZ);
      if (n <= 0 || send_all(s, buf, n))
        break;
      left -= n;
    }
  }
  close(fd);
}

static void do_ls(int s, const char *path) {
  char line[1200];
  DIR *d = opendir(path);
  if (!d) {
    snprintf(line, sizeof(line), "error: %s\n", strerror(errno));
    send_all(s, line, strlen(line));
    return;
  }
  struct dirent *de;
  while ((de = readdir(d))) {
    char full[1100];
    struct stat st;
    snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
    if (stat(full, &st))
      continue;
    snprintf(line, sizeof(line), "%12lld %s%s\n", (long long)st.st_size,
             de->d_name, S_ISDIR(st.st_mode) ? "/" : "");
    send_all(s, line, strlen(line));
  }
  closedir(d);
}

static void do_rm(int s, const char *path) {
  const char *msg = unlink(path) ? "error\n" : "ok\n";
  send_all(s, msg, strlen(msg));
}

int main(void) {
  int one = 1;
  signal(SIGPIPE, SIG_IGN);
  struct sockaddr_in sa = {0};
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(PORT);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) || listen(ls, 4)) {
    printf("fileserv: bind/listen on %d failed: %s\n", PORT, strerror(errno));
    return 1;
  }
  printf("fileserv listening on %d\n", PORT);
  fflush(stdout);

  char *buf = malloc(BUFSZ);
  for (;;) {
    int s = accept(ls, NULL, NULL);
    if (s < 0)
      continue;
    char cmd[1100];
    if (read_line(s, cmd, sizeof(cmd)) == 0) {
      if (!strncmp(cmd, "GET ", 4))
        do_get(s, cmd + 4, buf, UINT64_MAX);
      else if (!strncmp(cmd, "HEAD ", 5)) {
        char *end;
        uint64_t n = strtoull(cmd + 5, &end, 0);
        do_get(s, end + 1, buf, n);
      }
      else if (!strncmp(cmd, "LS ", 3))
        do_ls(s, cmd + 3);
      else if (!strncmp(cmd, "RM ", 3))
        do_rm(s, cmd + 3);
      else if (!strcmp(cmd, "QUIT")) {
        close(s);
        break;
      }
    }
    close(s);
  }
  close(ls);
  free(buf);
  return 0;
}
