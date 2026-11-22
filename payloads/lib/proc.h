#pragma once

#include <sys/types.h>
#include <stddef.h>

void *sysctl_alloc(int *mib, unsigned n, size_t *size);
int title_of(pid_t pid, char out[10]);
pid_t find_game(char title[10], int verbose);
