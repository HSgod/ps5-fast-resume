#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include <ps5/kernel.h>

#include "kobj.h"

#define PROC_IDT 0xc48
#define IDT_NPAGES 0x220
#define IDT_PER_PAGE 128
#define IDT_MAX_PAGES 64
#define IDE_SIZE 0x30
#define IDE_OBJ 0x10
#define IDE_TYPE 0x20
#define IDE_STATE 0x26
#define IDE_GEN 0x28
#define IDE_IN_USE 3

#define EVF_PATTERN 0x00
#define OSEM_COUNT 0x40

int kobj_walk(pid_t pid, kobj_cb cb, void *ctx) {
  uint64_t proc = kernel_get_proc(pid);
  if (!proc)
    return -1;
  uint64_t idt = kernel_getlong(proc + PROC_IDT);
  uint32_t npages = kernel_getint(idt + IDT_NPAGES);
  if (!idt || npages > IDT_MAX_PAGES)
    return -1;
  static uint8_t ents[IDT_PER_PAGE * IDE_SIZE];
  for (uint32_t pg = 0; pg < npages; pg++) {
    uint64_t page = kernel_getlong(idt + 8 * pg);
    if (!page || kernel_copyout(page, ents, sizeof(ents)))
      continue;
    for (int i = 0; i < IDT_PER_PAGE; i++) {
      uint8_t *e = ents + i * IDE_SIZE;
      if (*(uint16_t *)(e + IDE_STATE) != IDE_IN_USE)
        continue;
      uint32_t idx = pg * IDT_PER_PAGE + i;
      uint32_t id = ((*(uint32_t *)(e + IDE_GEN) << 13) & 0xffff) | idx;
      cb(id, *(uint16_t *)(e + IDE_TYPE), *(uint64_t *)(e + IDE_OBJ), ctx);
    }
  }
  return 0;
}

struct find_ctx {
  uint32_t id;
  uint16_t type;
  uint64_t obj;
};

static void find_cb(uint32_t id, uint16_t type, uint64_t obj, void *ctx) {
  struct find_ctx *f = ctx;
  if (id == f->id && type == f->type)
    f->obj = obj;
}

uint64_t kobj_find(pid_t pid, uint32_t id, uint16_t type) {
  struct find_ctx f = {id, type, 0};
  kobj_walk(pid, find_cb, &f);
  return f.obj;
}

uint64_t kobj_get_value(uint64_t obj, uint16_t type) {
  if (type == KOBJ_EVF)
    return kernel_getlong(obj + EVF_PATTERN);
  if (type == KOBJ_OSEM)
    return kernel_getint(obj + OSEM_COUNT);
  return 0;
}

int kobj_set_value(uint64_t obj, uint16_t type, uint64_t value) {
  if (type == KOBJ_EVF)
    return kernel_setlong(obj + EVF_PATTERN, value);
  if (type == KOBJ_OSEM)
    return kernel_setint(obj + OSEM_COUNT, (uint32_t)value);
  return -1;
}
