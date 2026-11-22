#pragma once

#include <sys/types.h>
#include <sys/types.h>
#include <sys/thr.h>
#include <stddef.h>

void debug_elevate(void);
void debug_restore_creds(void);
int debug_attach(pid_t pid);
void debug_detach(void);
int debug_drain(pid_t pid, int ms);
int debug_in_syscall(pid_t pid, lwpid_t lwp);
int debug_settle(pid_t pid, int timeout_ms);
int write_all(int fd, const void *buf, size_t len);
int read_all(int fd, void *buf, size_t len);
double now(void);
