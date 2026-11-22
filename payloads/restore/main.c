#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <signal.h>
#include <machine/reg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "debug.h"
#include "frd.h"
#include "kobj.h"
#include "vmobj.h"
#include "proc.h"

#define DUMP_DIR "/data/fast-resume"
#define CHUNK (4UL << 20)
#define MAX_THREADS 512

#ifdef FORCE
#define STRICT 0
#else
#define STRICT 1
#endif

#define G_DEFAULT 1
#define G_MODULE 2
#define G_DEVICE 4
#define G_OTHER 8

#ifndef GROUPS
#define GROUPS (G_DEFAULT | G_MODULE)
#endif

#define LOG_PATH DUMP_DIR "/restore.log"

struct thread_state {
  struct frd_thread t;
  struct reg r;
  struct fpreg fp;
  void *xstate;
  int live;
  int keep;
};

struct vm_entry {
  uint64_t start, end;
  int prot;
};

static pid_t g_pid;
static int g_log = -1;

static void logf_sync(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_sync(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (g_log >= 0 && n > 0) {
    write_all(g_log, line, n > (int)sizeof(line) ? (int)sizeof(line) : n);
    fsync(g_log);
  }
}

static int proc_write(uint64_t addr, const void *buf, size_t len) {
  struct ptrace_io_desc io = {PIOD_WRITE_D, (void *)addr, (void *)buf, len};
  if (ptrace(PT_IO, g_pid, (caddr_t)&io, 0))
    return -1;
  return io.piod_len == len ? 0 : -1;
}

static int group_of(uint32_t type) {
  switch (type) {
  case KVME_TYPE_DEFAULT:
    return G_DEFAULT;
  case 9:
    return G_MODULE;
  case KVME_TYPE_DEVICE:
    return G_DEVICE;
  }
  return G_OTHER;
}

struct group_stats {
  uint64_t regions, bytes, changed, written, failed;
};
static struct group_stats g_stats[4];

static int group_index(int g) {
  return g == G_DEFAULT ? 0 : g == G_MODULE ? 1 : g == G_DEVICE ? 2 : 3;
}
static struct thread_state g_threads[MAX_THREADS];
static uint32_t g_nthreads;
static struct vm_entry *g_vm;
static int g_nvm;

static int load_vmmap(void) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, g_pid};
  size_t size;
  char *buf = sysctl_alloc(mib, 4, &size);
  if (!buf)
    return -1;
  int cap = size / 64 + 1;
  g_vm = calloc(cap, sizeof(*g_vm));
  for (char *p = buf; p < buf + size && g_nvm < cap;) {
    struct kinfo_vmentry *e = (struct kinfo_vmentry *)p;
    if (e->kve_structsize <= 0)
      break;
    p += e->kve_structsize;
    g_vm[g_nvm++] = (struct vm_entry){e->kve_start, e->kve_end,
                                      e->kve_protection};
  }
  free(buf);
  return 0;
}

static int covered_writable(uint64_t start, uint64_t end) {
  int lo = 0, hi = g_nvm - 1, i = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (g_vm[mid].end <= start)
      lo = mid + 1;
    else if (g_vm[mid].start > start)
      hi = mid - 1;
    else {
      i = mid;
      break;
    }
  }
  if (i < 0)
    return 0;
  uint64_t at = start;
  for (; i < g_nvm && at < end; i++) {
    if (g_vm[i].start != at || !(g_vm[i].prot & KVME_PROT_WRITE))
      return 0;
    at = g_vm[i].end;
  }
  return at >= end;
}

static int load_threads(int fd, const struct frd_header *h) {
  if (h->nthreads > MAX_THREADS || h->reg_size != sizeof(struct reg) ||
      h->fpreg_size != sizeof(struct fpreg)) {
    printf("unsupported thread layout\n");
    return -1;
  }
  int n = ptrace(PT_GETNUMLWPS, g_pid, NULL, 0);
  lwpid_t *lwps = calloc(n > 0 ? n : 1, sizeof(*lwps));
  n = ptrace(PT_GETLWPLIST, g_pid, (caddr_t)lwps, n);

  int missing = 0;
  for (uint32_t i = 0; i < h->nthreads; i++) {
    struct thread_state *ts = &g_threads[i];
    if (read_all(fd, &ts->t, sizeof(ts->t)) ||
        read_all(fd, &ts->r, sizeof(ts->r)) ||
        read_all(fd, &ts->fp, sizeof(ts->fp)))
      return -1;
    if (ts->t.xstate_len) {
      ts->xstate = malloc(ts->t.xstate_len);
      if (read_all(fd, ts->xstate, ts->t.xstate_len))
        return -1;
    }
    for (int j = 0; j < n; j++)
      if (lwps[j] == (lwpid_t)ts->t.lwpid)
        ts->live = 1;
    if (!ts->live) {
      printf("  thread %u %s from dump no longer exists\n", ts->t.lwpid,
             ts->t.name);
      missing++;
    }
  }
  int extra = 0;
  for (int j = 0; j < n; j++) {
    int found = 0;
    for (uint32_t i = 0; i < h->nthreads; i++)
      found |= lwps[j] == (lwpid_t)g_threads[i].t.lwpid;
    if (!found) {
      printf("  thread %d not in dump\n", lwps[j]);
      extra++;
    }
  }
  g_nthreads = h->nthreads;
  printf("threads: dump %u, live %d, missing %d, new %d\n", h->nthreads, n,
         missing, extra);
  free(lwps);
  return missing + extra;
}

#define MAX_OBJ_TALLY 64

static uint64_t g_dmem_obj;

static int check_regions(int fd, const struct frd_header *h, uint64_t *bytes) {
  int bad = 0;
  uint64_t tally_obj[MAX_OBJ_TALLY] = {0}, tally_size[MAX_OBJ_TALLY] = {0};
  int ntally = 0;
  vmobj_load(g_pid);
  for (uint32_t i = 0; i < h->nregions; i++) {
    struct frd_region r;
    uint32_t nholes;
    if (read_all(fd, &r, sizeof(r)))
      return -1;
    lseek(fd, r.end - r.start, SEEK_CUR);
    if (read_all(fd, &nholes, sizeof(nholes)))
      return -1;
    lseek(fd, nholes * sizeof(struct frd_hole), SEEK_CUR);
    if (!(r.prot & KVME_PROT_WRITE))
      continue;
    if (!covered_writable(r.start, r.end)) {
      if (bad < 20)
        printf("  region 0x%lx-0x%lx not mapped writable now\n", r.start,
               r.end);
      bad++;
      continue;
    }
    *bytes += r.end - r.start;
    if (r.type == KVME_TYPE_DEVICE) {
      uint64_t obj = vmobj_of(r.start);
      int k = 0;
      while (k < ntally && tally_obj[k] != obj)
        k++;
      if (k == ntally && ntally < MAX_OBJ_TALLY)
        tally_obj[ntally++] = obj;
      if (k < MAX_OBJ_TALLY)
        tally_size[k] += r.end - r.start;
    }
  }
  for (int k = 0; k < ntally; k++) {
    printf("  device object 0x%lx %.1f MiB\n", tally_obj[k],
           tally_size[k] / 1048576.0);
  }
  uint64_t best = 0;
  for (int k = 0; k < ntally; k++)
    if (tally_size[k] > best) {
      best = tally_size[k];
      g_dmem_obj = tally_obj[k];
    }
  printf("direct memory object 0x%lx\n", g_dmem_obj);
  printf("writable regions not matching: %d\n", bad);
  return bad;
}

static int skip_device_region(const struct frd_region *r) {
  if (r->type != KVME_TYPE_DEVICE)
    return 0;
  uint64_t obj = vmobj_of(r->start);
  if (obj == g_dmem_obj)
    return 0;
  printf("  skip device 0x%lx-0x%lx obj 0x%lx (not direct memory)\n", r->start,
         r->end, obj);
  return 1;
}

static int write_regions(int fd, const struct frd_header *h, char *buf,
                         char *cur) {
  for (uint32_t i = 0; i < h->nregions; i++) {
    struct frd_region r;
    uint32_t nholes;
    if (read_all(fd, &r, sizeof(r)))
      return -1;
    uint64_t len = r.end - r.start;
    int g = group_of(r.type);
    int apply = (GROUPS & g) && (r.prot & KVME_PROT_WRITE) &&
                covered_writable(r.start, r.end) && !skip_device_region(&r);
    off_t data = lseek(fd, 0, SEEK_CUR);
    lseek(fd, len, SEEK_CUR);
    if (read_all(fd, &nholes, sizeof(nholes)))
      return -1;
    struct frd_hole *holes = NULL;
    if (nholes) {
      holes = malloc(nholes * sizeof(*holes));
      if (read_all(fd, holes, nholes * sizeof(*holes)))
        return -1;
    }
    off_t next = lseek(fd, 0, SEEK_CUR);
    if (apply) {
      struct group_stats *st = &g_stats[group_index(g)];
      st->regions++;
      st->bytes += len;
      logf_sync("region %u 0x%lx-0x%lx type %u prot %x\n", i, r.start, r.end,
                r.type, r.prot);
      lseek(fd, data, SEEK_SET);
      uint32_t hi = 0;
      for (uint64_t off = 0; off < len; off += CHUNK) {
        uint64_t n = len - off > CHUNK ? CHUNK : len - off;
        if (read_all(fd, buf, n))
          return -1;
        int have_cur = !kernel_proc_copyout(g_pid, r.start + off, cur, n);
        for (uint64_t po = 0; po < n; po += PAGE_SIZE) {
          uint64_t a = off + po;
          while (hi < nholes && holes[hi].offset + holes[hi].len <= a)
            hi++;
          if (hi < nholes && holes[hi].offset <= a)
            continue;
          if (have_cur && !memcmp(buf + po, cur + po, PAGE_SIZE))
            continue;
          st->changed += PAGE_SIZE;
          if (proc_write(r.start + a, buf + po, PAGE_SIZE))
            st->failed++;
          else
            st->written += PAGE_SIZE;
        }
      }
      lseek(fd, next, SEEK_SET);
    }
    free(holes);
  }
  logf_sync("memory done\n");
  return 0;
}

static struct kobj *g_kobjs;
static uint32_t g_nkobjs;

static int load_kobjs(int fd, const struct frd_header *h) {
  g_nkobjs = h->nkobjs;
  if (!g_nkobjs)
    return 0;
  g_kobjs = malloc(g_nkobjs * sizeof(*g_kobjs));
  if (read_all(fd, g_kobjs, g_nkobjs * sizeof(*g_kobjs)))
    return -1;
  int missing = 0;
  for (uint32_t i = 0; i < g_nkobjs; i++)
    if (!kobj_find(g_pid, g_kobjs[i].id, g_kobjs[i].type))
      missing++;
  printf("kernel objects: dump %u, missing now %d\n", g_nkobjs, missing);
  return missing;
}

static int set_kobjs(void) {
  int changed = 0, failed = 0;
  for (uint32_t i = 0; i < g_nkobjs; i++) {
    struct kobj *k = &g_kobjs[i];
    uint64_t obj = kobj_find(g_pid, k->id, k->type);
    if (!obj)
      continue;
    uint64_t cur = kobj_get_value(obj, k->type);
    if (cur == k->value)
      continue;
    printf("  %s %#06x 0x%lx -> 0x%lx\n", k->type == KOBJ_EVF ? "evf " : "osem",
           k->id, cur, k->value);
    if (kobj_set_value(obj, k->type, k->value))
      failed++;
    else
      changed++;
  }
  printf("kernel objects changed %d, failed %d\n", changed, failed);
  return failed;
}

static void mark_kept_threads(void) {
  int kept = 0, risky = 0;
  for (uint32_t i = 0; i < g_nthreads; i++) {
    struct thread_state *ts = &g_threads[i];
    if (!ts->live || !debug_in_syscall(g_pid, ts->t.lwpid))
      continue;
    struct reg cur;
    ptrace(PT_GETREGS, ts->t.lwpid, (caddr_t)&cur, 0);
    if ((ts->t.flags & FRD_THREAD_IN_SYSCALL) && cur.r_rip == ts->r.r_rip &&
        cur.r_rsp == ts->r.r_rsp) {
      ts->keep = 1;
      kept++;
    } else {
      printf("  lwp %u %s is in a syscall now but not at its dump site\n",
             ts->t.lwpid, ts->t.name);
      risky++;
    }
  }
  printf("threads left sleeping in place: %d, mismatched: %d\n", kept, risky);
}

static int set_threads(void) {
  int failed = 0;
  for (uint32_t i = 0; i < g_nthreads; i++) {
    struct thread_state *ts = &g_threads[i];
    lwpid_t l = ts->t.lwpid;
    if (!ts->live || ts->keep)
      continue;
    if (ptrace(PT_SETREGS, l, (caddr_t)&ts->r, 0)) {
      printf("  lwp %d PT_SETREGS: %s\n", l, strerror(errno));
      failed++;
    }
    if (ptrace(PT_SETFPREGS, l, (caddr_t)&ts->fp, 0)) {
      printf("  lwp %d PT_SETFPREGS: %s\n", l, strerror(errno));
      failed++;
    }
    if (ts->t.xstate_len &&
        ptrace(PT_SETXSTATE, l, ts->xstate, ts->t.xstate_len)) {
      printf("  lwp %d PT_SETXSTATE: %s\n", l, strerror(errno));
      failed++;
    }
    if (ptrace(PT_SETFSBASE, l, (caddr_t)&ts->t.fsbase, 0) ||
        ptrace(PT_SETGSBASE, l, (caddr_t)&ts->t.gsbase, 0)) {
      printf("  lwp %d PT_SETFS/GSBASE: %s\n", l, strerror(errno));
      failed++;
    }
  }
  return failed;
}

#ifdef TRACE
static void hexline(uint64_t addr, size_t len) {
  unsigned char b[256];
  if (len > sizeof(b))
    len = sizeof(b);
  if (kernel_proc_copyout(g_pid, addr, b, len)) {
    printf("    0x%lx unreadable\n", addr);
    return;
  }
  for (size_t i = 0; i < len; i += 16) {
    printf("    %012lx:", addr + i);
    for (size_t j = i; j < i + 16 && j < len; j++)
      printf(" %02x", b[j]);
    printf("\n");
  }
}

static void print_thread(lwpid_t l, int full) {
  struct ptrace_lwpinfo li = {0};
  struct reg r = {0};
  ptrace(PT_LWPINFO, l, (caddr_t)&li, sizeof(li));
  ptrace(PT_GETREGS, l, (caddr_t)&r, 0);
  printf("  lwp %d %-20s rip 0x%lx rsp 0x%lx rax 0x%lx\n", l, li.pl_tdname,
         r.r_rip, r.r_rsp, r.r_rax);
  if (!full)
    return;
  printf("  rbx %lx rcx %lx rdx %lx rsi %lx rdi %lx rbp %lx\n", r.r_rbx, r.r_rcx,
         r.r_rdx, r.r_rsi, r.r_rdi, r.r_rbp);
  printf("  r8 %lx r9 %lx r10 %lx r11 %lx r12 %lx r13 %lx r14 %lx r15 %lx\n",
         r.r_r8, r.r_r9, r.r_r10, r.r_r11, r.r_r12, r.r_r13, r.r_r14, r.r_r15);
  printf("  code:\n");
  hexline((r.r_rip & ~0xfUL) - 0x40, 0x80);
  printf("  stack:\n");
  hexline(r.r_rsp, 0x100);
}

static void print_all_threads(void) {
  int n = ptrace(PT_GETNUMLWPS, g_pid, NULL, 0);
  lwpid_t *l = calloc(n > 0 ? n : 1, sizeof(*l));
  n = ptrace(PT_GETLWPLIST, g_pid, (caddr_t)l, n);
  for (int i = 0; i < n; i++)
    print_thread(l[i], 0);
  free(l);
}

static void trace_after_restore(int seconds) {
  printf("tracing for %ds\n", seconds);
  fflush(stdout);
  if (ptrace(PT_CONTINUE, g_pid, (caddr_t)1, 0)) {
    printf("PT_CONTINUE: %s\n", strerror(errno));
    return;
  }
  double end = now() + seconds;
  while (now() < end) {
    int st;
    pid_t r = waitpid(g_pid, &st, WNOHANG);
    if (r == 0) {
      usleep(20000);
      continue;
    }
    if (r < 0) {
      printf("waitpid: %s\n", strerror(errno));
      return;
    }
    if (WIFEXITED(st) || WIFSIGNALED(st)) {
      printf("game exited (status 0x%x)\n", st);
      return;
    }
    if (!WIFSTOPPED(st))
      continue;
    int sig = WSTOPSIG(st);
    struct ptrace_lwpinfo li = {0};
    ptrace(PT_LWPINFO, g_pid, (caddr_t)&li, sizeof(li));
    printf("signal %d in lwp %d %s, si_code %d si_addr %p (t=%.2fs)\n", sig,
           li.pl_lwpid, li.pl_tdname, li.pl_siginfo.si_code,
           li.pl_siginfo.si_addr, seconds - (end - now()));
    print_thread(li.pl_lwpid, 1);
    fflush(stdout);
    if (sig == SIGSTOP || sig == SIGTRAP) {
      ptrace(PT_CONTINUE, g_pid, (caddr_t)1, 0);
      continue;
    }
    printf("all threads at fault:\n");
    print_all_threads();
    fflush(stdout);
    ptrace(PT_DETACH, g_pid, (caddr_t)1, sig);
    return;
  }
  printf("no fault within %ds, stopping to sample threads\n", seconds);
  kill(g_pid, SIGSTOP);
  int st;
  waitpid(g_pid, &st, 0);
  print_all_threads();
}
#endif

int main(void) {
  char title[10] = {0};
  char path[128];
  struct frd_header h;
  double t0 = now();

  g_pid = find_game(title, 1);
  if (g_pid < 0) {
    printf("no game process found\n");
    return 1;
  }
  snprintf(path, sizeof(path), DUMP_DIR "/%s.frd", title);
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("open %s: %s\n", path, strerror(errno));
    return 1;
  }
  if (read_all(fd, &h, sizeof(h)) || memcmp(h.magic, FRD_MAGIC, 8) ||
      h.version != FRD_VERSION || strncmp(h.title, title, 10)) {
    printf("%s: not a dump of %s\n", path, title);
    return 1;
  }
  printf("game pid %d title %s, dump pid %u, %u threads, %u regions%s\n",
         g_pid, title, h.pid, h.nthreads, h.nregions,
         STRICT ? "" : " (force)");
  if (h.pid != (uint32_t)g_pid) {
    printf("dump is from another process instance (pid %u)\n", h.pid);
    return 1;
  }

  if (debug_attach(g_pid))
    return 1;
  double t_stop = now();
  debug_drain(g_pid, 300);
  debug_settle(g_pid, 1000);

  if (load_vmmap()) {
    printf("KERN_PROC_VMMAP failed: %s\n", strerror(errno));
    return 1;
  }
  int thread_diff = load_threads(fd, &h);
  if (thread_diff < 0) {
    printf("dump read error\n");
    return 1;
  }
  int kobj_diff = load_kobjs(fd, &h);
  if (kobj_diff < 0) {
    printf("dump read error\n");
    return 1;
  }
  off_t regions_at = lseek(fd, 0, SEEK_CUR);
  uint64_t bytes = 0;
  int region_diff = check_regions(fd, &h, &bytes);
  if (region_diff < 0) {
    printf("dump read error\n");
    return 1;
  }
  if (STRICT && (thread_diff || region_diff || kobj_diff)) {
    printf("state differs from dump, nothing written (use restore_force)\n");
    return 1;
  }

  g_log = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  logf_sync("restore %s pid %d groups %x\n", title, g_pid, GROUPS);
  printf("groups 0x%x, candidate %.1f MiB\n", GROUPS, bytes / 1048576.0);
  fflush(stdout);
  char *buf = malloc(CHUNK);
  char *cur = malloc(CHUNK);
  lseek(fd, regions_at, SEEK_SET);
  if (write_regions(fd, &h, buf, cur)) {
    printf("dump read error during write\n");
    return 1;
  }
  double t_mem = now();
  logf_sync("kobjs\n");
  set_kobjs();
  logf_sync("threads\n");
  mark_kept_threads();
  int tfail = set_threads();
  logf_sync("threads done\n");
#ifdef TRACE
  trace_after_restore(60);
#endif
  debug_detach();

  static const char *names[4] = {"default", "module", "device", "other"};
  for (int i = 0; i < 4; i++)
    if (g_stats[i].regions)
      printf("  %-8s regions %6lu  size %8.1f MiB  changed %8.1f MiB  "
             "written %8.1f MiB  failed pages %lu\n",
             names[i], g_stats[i].regions, g_stats[i].bytes / 1048576.0,
             g_stats[i].changed / 1048576.0, g_stats[i].written / 1048576.0,
             g_stats[i].failed);
  printf("failed thread ops %d\n", tfail);
  logf_sync("done\n");
  close(g_log);
  printf("memory write %.2fs, game frozen %.2fs, total %.2fs\n",
         t_mem - t_stop, now() - t_stop, now() - t0);
  free(buf);
  free(cur);
  close(fd);
  return 0;
}
