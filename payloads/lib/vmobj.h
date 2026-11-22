#pragma once

#include <sys/types.h>
#include <stdint.h>

struct vm_obj_range {
  uint64_t start, end, obj;
};

int vmobj_load(pid_t pid);
uint64_t vmobj_of(uint64_t addr);
