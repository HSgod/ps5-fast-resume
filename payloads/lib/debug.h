#pragma once

#include <sys/types.h>
#include <stddef.h>

void debug_elevate(void);
void debug_restore_creds(void);
int debug_attach(pid_t pid);
void debug_detach(void);
int debug_drain(pid_t pid, int ms);
int write_all(int fd, const void *buf, size_t len);
int read_all(int fd, void *buf, size_t len);
double now(void);
