#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <machine/reg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "debug.h"
#include "frd.h"
#include "kobj.h"
#include "proc.h"

#define DUMP_DIR "/data/fast-resume"
#define CHUNK (4UL << 20)
#define MAX_THREADS 512
#define MAX_HOLES 65536

static pid_t g_pid = -1;

static int dump_threads(int fd, uint32_t *count) {
  int n = ptrace(PT_GETNUMLWPS, g_pid, NULL, 0);
  if (n <= 0) {
    printf("PT_GETNUMLWPS failed: %s\n", strerror(errno));
    return -1;
  }
  if (n > MAX_THREADS)
    n = MAX_THREADS;
  lwpid_t *lwps = calloc(n, sizeof(*lwps));
  n = ptrace(PT_GETLWPLIST, g_pid, (caddr_t)lwps, n);
  if (n <= 0) {
    printf("PT_GETLWPLIST failed: %s\n", strerror(errno));
    free(lwps);
    return -1;
  }

  struct ptrace_xstate_info xi = {0};
  if (ptrace(PT_GETXSTATE_INFO, lwps[0], (caddr_t)&xi, sizeof(xi))) {
    printf("PT_GETXSTATE_INFO failed: %s\n", strerror(errno));
    xi.xsave_len = 0;
  }
  void *xbuf = xi.xsave_len ? malloc(xi.xsave_len) : NULL;

  for (int i = 0; i < n; i++) {
    struct frd_thread t = {0};
    struct ptrace_lwpinfo li = {0};
    struct reg r = {0};
    struct fpreg fp = {0};

    t.lwpid = lwps[i];
    if (!ptrace(PT_LWPINFO, lwps[i], (caddr_t)&li, sizeof(li)))
      strncpy(t.name, li.pl_tdname, sizeof(t.name) - 1);
    if (ptrace(PT_GETREGS, lwps[i], (caddr_t)&r, 0))
      printf("  lwp %d PT_GETREGS failed: %s\n", lwps[i], strerror(errno));
    if (ptrace(PT_GETFPREGS, lwps[i], (caddr_t)&fp, 0))
      printf("  lwp %d PT_GETFPREGS failed: %s\n", lwps[i], strerror(errno));
    ptrace(PT_GETFSBASE, lwps[i], (caddr_t)&t.fsbase, 0);
    ptrace(PT_GETGSBASE, lwps[i], (caddr_t)&t.gsbase, 0);
    if (xbuf) {
      memset(xbuf, 0, xi.xsave_len);
      if (!ptrace(PT_GETXSTATE, lwps[i], xbuf, xi.xsave_len)) {
        t.xstate_len = xi.xsave_len;
        t.xsave_mask = xi.xsave_mask;
      }
    }

    printf("  lwp %6d %-20s rip 0x%012lx rsp 0x%012lx fs 0x%012lx\n",
           t.lwpid, t.name, r.r_rip, r.r_rsp, t.fsbase);

    if (write_all(fd, &t, sizeof(t)) || write_all(fd, &r, sizeof(r)) ||
        write_all(fd, &fp, sizeof(fp)) ||
        (t.xstate_len && write_all(fd, xbuf, t.xstate_len))) {
      free(xbuf);
      free(lwps);
      return -1;
    }
  }
  *count = n;
  free(xbuf);
  free(lwps);
  return 0;
}

#define MAX_KOBJS 4096

struct kobj_list {
  struct kobj items[MAX_KOBJS];
  uint32_t n;
};

static void collect_kobj(uint32_t id, uint16_t type, uint64_t obj, void *ctx) {
  struct kobj_list *l = ctx;
  if ((type != KOBJ_EVF && type != KOBJ_OSEM) || l->n >= MAX_KOBJS)
    return;
  l->items[l->n++] = (struct kobj){id, type, 0, kobj_get_value(obj, type)};
}

static struct kobj_list g_kobjs;

static struct frd_hole holes[MAX_HOLES];

static int dump_region(int fd, struct kinfo_vmentry *e, char *buf,
                       uint64_t *ok_bytes, uint64_t *bad_bytes) {
  struct frd_region rh = {e->kve_start, e->kve_end, e->kve_protection,
                          e->kve_type, e->kve_flags, 0};
  uint32_t nholes = 0;

  if (write_all(fd, &rh, sizeof(rh)))
    return -1;

  for (uint64_t off = 0; off < e->kve_end - e->kve_start; off += CHUNK) {
    uint64_t len = e->kve_end - e->kve_start - off;
    if (len > CHUNK)
      len = CHUNK;
    if (kernel_proc_copyout(g_pid, e->kve_start + off, buf, len)) {
      for (uint64_t po = 0; po < len; po += PAGE_SIZE) {
        if (!kernel_proc_copyout(g_pid, e->kve_start + off + po, buf + po,
                                 PAGE_SIZE)) {
          *ok_bytes += PAGE_SIZE;
          continue;
        }
        memset(buf + po, 0, PAGE_SIZE);
        *bad_bytes += PAGE_SIZE;
        if (nholes && holes[nholes - 1].offset + holes[nholes - 1].len ==
                          off + po)
          holes[nholes - 1].len += PAGE_SIZE;
        else if (nholes < MAX_HOLES)
          holes[nholes++] = (struct frd_hole){off + po, PAGE_SIZE};
      }
    } else {
      *ok_bytes += len;
    }
    if (write_all(fd, buf, len))
      return -1;
  }

  if (write_all(fd, &nholes, sizeof(nholes)) ||
      write_all(fd, holes, nholes * sizeof(holes[0])))
    return -1;
  return 0;
}

int main(void) {
  char title[10] = {0};
  char path[128];
  char tmp[160];
  double t0 = now();

  g_pid = find_game(title, 1);
  if (g_pid < 0) {
    printf("no game process found\n");
    return 1;
  }
  printf("game pid %d title %s\n", g_pid, title);

  mkdir(DUMP_DIR, 0777);
  snprintf(path, sizeof(path), DUMP_DIR "/%s.frd", title);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    printf("open %s: %s\n", tmp, strerror(errno));
    return 1;
  }
  char *buf = malloc(CHUNK);

  if (debug_attach(g_pid))
    return 1;
  double t_stop = now();
  debug_drain(g_pid, 300);
  printf("game stopped (%.2fs)\n", t_stop - t0);

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, g_pid};
  size_t vsize;
  char *vm = sysctl_alloc(mib, 4, &vsize);
  if (!vm) {
    printf("KERN_PROC_VMMAP failed: %s\n", strerror(errno));
    return 1;
  }

  struct frd_header h = {0};
  memcpy(h.magic, FRD_MAGIC, 8);
  h.version = FRD_VERSION;
  h.pid = g_pid;
  memcpy(h.title, title, 10);
  h.time = time(NULL);
  h.page_size = PAGE_SIZE;
  h.reg_size = sizeof(struct reg);
  h.fpreg_size = sizeof(struct fpreg);
  if (write_all(fd, &h, sizeof(h)))
    goto io_error;

  printf("threads:\n");
  uint32_t nthreads = 0;
  if (dump_threads(fd, &nthreads))
    goto io_error;
  h.nthreads = nthreads;

  g_kobjs.n = 0;
  kobj_walk(g_pid, collect_kobj, &g_kobjs);
  h.nkobjs = g_kobjs.n;
  if (write_all(fd, g_kobjs.items, g_kobjs.n * sizeof(struct kobj)))
    goto io_error;
  printf("kernel objects: %u evf/osem\n", g_kobjs.n);

  uint64_t ok = 0, bad = 0;
  for (char *p = vm; p < vm + vsize;) {
    struct kinfo_vmentry *e = (struct kinfo_vmentry *)p;
    if (e->kve_structsize <= 0)
      break;
    p += e->kve_structsize;
    if (!(e->kve_protection & (KVME_PROT_READ | KVME_PROT_WRITE)) ||
        e->kve_end <= e->kve_start)
      continue;
    if (dump_region(fd, e, buf, &ok, &bad))
      goto io_error;
    h.nregions++;
  }
  double t_mem = now();
  debug_detach();
  double t_resume = now();

  if (pwrite(fd, &h, sizeof(h), 0) != sizeof(h))
    goto io_error;
  close(fd);
  rename(tmp, path);

  printf("regions %u, read %.1f MiB, unreadable %.1f MiB\n", h.nregions,
         ok / 1048576.0, bad / 1048576.0);
  printf("memory read %.2fs, game frozen %.2fs, total %.2fs, file %s\n",
         t_mem - t_stop, t_resume - t_stop, now() - t0, path);
  free(vm);
  free(buf);
  return 0;

io_error:
  printf("write error: %s\n", strerror(errno));
  close(fd);
  unlink(tmp);
  return 1;
}
