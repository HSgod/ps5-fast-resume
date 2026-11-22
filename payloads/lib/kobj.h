#pragma once

#include <sys/types.h>
#include <stdint.h>

#define KOBJ_EVF 0x110
#define KOBJ_OSEM 0x120

struct kobj {
  uint32_t id;
  uint16_t type;
  uint16_t reserved;
  uint64_t value;
} __attribute__((packed));

typedef void (*kobj_cb)(uint32_t id, uint16_t type, uint64_t obj, void *ctx);

int kobj_walk(pid_t pid, kobj_cb cb, void *ctx);
uint64_t kobj_find(pid_t pid, uint32_t id, uint16_t type);
uint64_t kobj_get_value(uint64_t obj, uint16_t type);
int kobj_set_value(uint64_t obj, uint16_t type, uint64_t value);
