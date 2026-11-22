#pragma once

#include <stdint.h>

#define FRD_MAGIC "FRDUMP\0\1"
#define FRD_VERSION 2

struct frd_header {
  char magic[8];
  uint32_t version;
  uint32_t pid;
  char title[16];
  uint64_t time;
  uint32_t nthreads;
  uint32_t nregions;
  uint32_t page_size;
  uint32_t reg_size;
  uint32_t fpreg_size;
  uint32_t nkobjs;
} __attribute__((packed));

struct frd_thread {
  uint32_t lwpid;
  char name[24];
  uint32_t xstate_len;
  uint64_t xsave_mask;
  uint64_t fsbase;
  uint64_t gsbase;
} __attribute__((packed));

struct frd_region {
  uint64_t start;
  uint64_t end;
  uint32_t prot;
  uint32_t type;
  uint32_t flags;
  uint32_t reserved;
} __attribute__((packed));

struct frd_hole {
  uint64_t offset;
  uint64_t len;
} __attribute__((packed));
