#!/usr/bin/env python3
import argparse
import mmap
import struct
import sys

HDR = struct.Struct("<8sII16sQIIIIII")
THR = struct.Struct("<I24sIQQQ")
THR3 = struct.Struct("<I24sIQQQII")
REG = struct.Struct("<QQIIII")
HOLE = struct.Struct("<QQ")
REGS64 = ["r15", "r14", "r13", "r12", "r11", "r10", "r9", "r8", "rdi", "rsi",
          "rbp", "rbx", "rdx", "rcx", "rax"]
TYPES = {0: "none", 1: "default", 2: "vnode", 3: "swap", 4: "device",
         5: "phys", 6: "dead", 7: "sg", 8: "mgtdev"}


class Dump:
    def __init__(self, path, threads_only=False):
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        (magic, self.version, self.pid, title, self.time, self.nthreads,
         self.nregions, self.page_size, self.reg_size, self.fpreg_size,
         _) = HDR.unpack_from(self.m, 0)
        if magic != b"FRDUMP\0\1" or self.version not in (1, 2, 3):
            sys.exit(f"{path}: bad magic")
        self.title = title.rstrip(b"\0").decode()
        off = HDR.size
        self.threads = []
        for _ in range(self.nthreads):
            if self.version >= 3:
                lwpid, name, xlen, xmask, fsbase, gsbase, tflags, _ = THR3.unpack_from(self.m, off)
                off += THR3.size
            else:
                lwpid, name, xlen, xmask, fsbase, gsbase = THR.unpack_from(self.m, off)
                tflags = 0
                off += THR.size
            regs = struct.unpack_from("<15QIHHIHHQQQQQ", self.m, off)
            off += self.reg_size + self.fpreg_size + xlen
            self.threads.append(dict(lwpid=lwpid, name=name.rstrip(b"\0").decode(errors="replace"),
                                     xlen=xlen, xmask=xmask, fsbase=fsbase,
                                     gsbase=gsbase, flags=tflags, regs=regs))
        (self.nkobjs,) = struct.unpack_from("<I", self.m, HDR.size - 4)
        self.kobjs = []
        for _ in range(self.nkobjs if self.version >= 2 else 0):
            kid, ktype, _r, kval = struct.unpack_from("<IHHQ", self.m, off)
            off += 16
            self.kobjs.append((kid, ktype, kval))
        self.regions = []
        for _ in range(0 if threads_only else self.nregions):
            start, end, prot, typ, flags, _ = REG.unpack_from(self.m, off)
            off += REG.size
            data = off
            off += end - start
            (nholes,) = struct.unpack_from("<I", self.m, off)
            off += 4
            holes = [HOLE.unpack_from(self.m, off + i * HOLE.size) for i in range(nholes)]
            off += nholes * HOLE.size
            self.regions.append(dict(start=start, end=end, prot=prot, type=typ,
                                     flags=flags, data=data, holes=holes))

    def read(self, addr, n):
        for r in self.regions:
            if r["start"] <= addr and addr + n <= r["end"]:
                o = r["data"] + addr - r["start"]
                return self.m[o:o + n]
        return None


def prot_str(p):
    return ("r" if p & 1 else "-") + ("w" if p & 2 else "-") + ("x" if p & 4 else "-")


def cmd_info(d, a):
    print(f"title {d.title} pid {d.pid} version {d.version} page 0x{d.page_size:x}")
    print(f"threads {d.nthreads} regions {d.nregions} kernel objects {len(d.kobjs)}")
    for t in d.threads:
        r = t["regs"]
        print(f"  lwp {t['lwpid']:6} {t['name']:20} rip {r[21]:#014x} rsp {r[24]:#014x} "
              f"fs {t['fsbase']:#014x} xsave {t['xlen']}")
        if a.regs:
            print("    " + " ".join(f"{n}={v:#x}" for n, v in zip(REGS64, r[:15])))
    tot = sum(r["end"] - r["start"] for r in d.regions)
    holes = sum(h[1] for r in d.regions for h in r["holes"])
    print(f"memory {tot / 2**20:.1f} MiB, unreadable {holes / 2**20:.1f} MiB")
    by = {}
    for r in d.regions:
        k = (TYPES.get(r["type"], f"t{r['type']}"), prot_str(r["prot"]))
        n, sz, hz = by.get(k, (0, 0, 0))
        by[k] = (n + 1, sz + r["end"] - r["start"], hz + sum(h[1] for h in r["holes"]))
    for k, (n, sz, hz) in sorted(by.items(), key=lambda x: -x[1][1]):
        print(f"  {k[0]:8} {k[1]} n={n:<5} {sz / 2**20:9.1f} MiB  holes {hz / 2**20:8.1f} MiB")


FMT = {"u8": "<B", "u16": "<H", "u32": "<I", "i32": "<i", "u64": "<Q", "f32": "<f", "f64": "<d"}


def search(d, fmt, value, writable_only=True, limit=None):
    pat = struct.pack(fmt, value)
    size = struct.calcsize(fmt)
    hits = []
    for r in d.regions:
        if writable_only and not r["prot"] & 2:
            continue
        o, end = r["data"], r["data"] + r["end"] - r["start"]
        while True:
            i = d.m.find(pat, o, end)
            if i < 0:
                break
            if (i - r["data"]) % size == 0:
                hits.append(r["start"] + i - r["data"])
                if limit and len(hits) >= limit:
                    return hits
            o = i + 1
    return hits


def cmd_find(d, a):
    v = float(a.value) if a.type.startswith("f") else int(a.value, 0)
    hits = search(d, FMT[a.type], v, not a.all)
    print(f"{len(hits)} hits")
    for h in hits[: a.max]:
        print(f"  {h:#014x}")


def cmd_narrow(d, a):
    fmt = FMT[a.type]
    size = struct.calcsize(fmt)
    dumps = [d] + [Dump(p) for p in a.more]
    values = [float(v) if a.type.startswith("f") else int(v, 0) for v in a.values]
    if len(values) != len(dumps):
        sys.exit("need one value per dump")
    hits = search(d, fmt, values[0], not a.all)
    print(f"{a.dump}: {len(hits)} hits for {values[0]}")
    for dd, v, p in zip(dumps[1:], values[1:], a.more):
        keep = []
        for h in hits:
            b = dd.read(h, size)
            if b is not None and struct.unpack(fmt, b)[0] == v:
                keep.append(h)
        hits = keep
        print(f"{p}: {len(hits)} hits for {v}")
    for h in hits[: a.max]:
        print(f"  {h:#014x}")


def cmd_peek(d, a):
    addr = int(a.addr, 0)
    b = d.read(addr, a.len)
    if b is None:
        sys.exit("address not in dump")
    for i in range(0, len(b), 16):
        row = b[i:i + 16]
        print(f"{addr + i:#014x}: {row.hex(' '):48}  "
              + "".join(chr(c) if 32 <= c < 127 else "." for c in row))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("--regs", action="store_true"); p.set_defaults(fn=cmd_info)
    p = sub.add_parser("find"); p.add_argument("type", choices=FMT); p.add_argument("value")
    p.add_argument("--all", action="store_true"); p.add_argument("--max", type=int, default=50); p.set_defaults(fn=cmd_find)
    p = sub.add_parser("narrow"); p.add_argument("type", choices=FMT); p.add_argument("values", nargs="+")
    p.add_argument("--more", nargs="*", default=[]); p.add_argument("--all", action="store_true")
    p.add_argument("--max", type=int, default=50); p.set_defaults(fn=cmd_narrow)
    p = sub.add_parser("peek"); p.add_argument("addr"); p.add_argument("--len", type=int, default=128); p.set_defaults(fn=cmd_peek)
    a = ap.parse_args()
    a.fn(Dump(a.dump), a)


if __name__ == "__main__":
    main()
