#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "debug.h"

#define DEBUGGER_AUTHID 0x4800000000010003UL

static pid_t g_pid = -1;
static int g_attached;
static int g_elevated;
static uint64_t g_authid;
static uint8_t g_caps[16];

void debug_elevate(void) {
  uint8_t caps[16];
  g_authid = kernel_get_ucred_authid(getpid());
  kernel_get_ucred_caps(getpid(), g_caps);
  memset(caps, 0xff, sizeof(caps));
  kernel_set_ucred_caps(getpid(), caps);
  kernel_set_ucred_authid(getpid(), DEBUGGER_AUTHID);
  g_elevated = 1;
}

void debug_restore_creds(void) {
  if (!g_elevated)
    return;
  kernel_set_ucred_authid(getpid(), g_authid);
  kernel_set_ucred_caps(getpid(), g_caps);
  g_elevated = 0;
}

void debug_detach(void) {
  if (g_attached) {
    if (ptrace(PT_DETACH, g_pid, (caddr_t)1, 0))
      printf("PT_DETACH failed: %s\n", strerror(errno));
    else
      printf("detached, game resumed\n");
    g_attached = 0;
  }
  debug_restore_creds();
}

int debug_attach(pid_t pid) {
  int st;
  g_pid = pid;
  debug_elevate();
  if (ptrace(PT_ATTACH, pid, NULL, 0)) {
    printf("PT_ATTACH failed: %s\n", strerror(errno));
    debug_restore_creds();
    return -1;
  }
  g_attached = 1;
  atexit(debug_detach);
  if (waitpid(pid, &st, 0) < 0)
    printf("waitpid: %s\n", strerror(errno));
  return 0;
}

int write_all(int fd, const void *buf, size_t len) {
  const char *p = buf;
  while (len) {
    ssize_t n = write(fd, p, len);
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

int read_all(int fd, void *buf, size_t len) {
  char *p = buf;
  while (len) {
    ssize_t n = read(fd, p, len);
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

double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int drain_keeps_running(const char *name) {
  if (!strncmp(name, "AgcSubmission", 13))
    return 0;
  return !strncmp(name, "Agc", 3) || !strncmp(name, "SceVideoOut", 11);
}

int debug_drain(pid_t pid, int ms) {
  int n = ptrace(PT_GETNUMLWPS, pid, NULL, 0);
  if (n <= 0)
    return -1;
  lwpid_t *l = calloc(n, sizeof(*l));
  n = ptrace(PT_GETLWPLIST, pid, (caddr_t)l, n);
  int running = 0;
  for (int i = 0; i < n; i++) {
    struct ptrace_lwpinfo li = {0};
    ptrace(PT_LWPINFO, l[i], (caddr_t)&li, sizeof(li));
    if (drain_keeps_running(li.pl_tdname)) {
      running++;
      continue;
    }
    if (ptrace(PT_SUSPEND, l[i], NULL, 0))
      printf("PT_SUSPEND %d: %s\n", l[i], strerror(errno));
  }
  printf("drain: %d of %d threads running for %d ms\n", running, n, ms);
  if (ptrace(PT_CONTINUE, pid, (caddr_t)1, 0)) {
    printf("PT_CONTINUE: %s\n", strerror(errno));
  } else {
    usleep(ms * 1000);
    kill(pid, SIGSTOP);
    int st;
    if (waitpid(pid, &st, 0) < 0)
      printf("waitpid after drain: %s\n", strerror(errno));
  }
  for (int i = 0; i < n; i++)
    ptrace(PT_RESUME, l[i], NULL, 0);
  free(l);
  return 0;
}
