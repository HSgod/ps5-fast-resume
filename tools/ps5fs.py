#!/usr/bin/env python3
import os
import socket
import struct
import sys
import time

PORT = 9031


def connect(host):
    s = socket.create_connection((host, PORT), timeout=30)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
    return s


def recv_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = s.recv(min(n - len(buf), 1 << 20))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return bytes(buf)


def cmd_text(host, line):
    with connect(host) as s:
        s.sendall(line.encode() + b"\n")
        out = bytearray()
        while chunk := s.recv(65536):
            out += chunk
    sys.stdout.write(out.decode(errors="replace"))


def cmd_get(host, remote, local, limit=None):
    with connect(host) as s:
        s.sendall((f"HEAD {limit} {remote}\n" if limit else f"GET {remote}\n").encode())
        (size,) = struct.unpack("<Q", recv_exact(s, 8))
        if size == 0xFFFFFFFFFFFFFFFF:
            sys.exit(f"remote file not found: {remote}")
        os.makedirs(os.path.dirname(os.path.abspath(local)), exist_ok=True)
        t0 = time.time()
        done = 0
        with open(local + ".part", "wb") as f:
            while done < size:
                chunk = s.recv(min(size - done, 4 << 20))
                if not chunk:
                    sys.exit("connection closed early")
                f.write(chunk)
                done += len(chunk)
                dt = max(time.time() - t0, 1e-3)
                print(f"\r{done >> 20}/{size >> 20} MiB  {done / dt / 1e6:.1f} MB/s",
                      end="", flush=True)
        os.replace(local + ".part", local)
        print()


def main():
    host = os.environ.get("PS5_HOST")
    if not host or len(sys.argv) < 3:
        sys.exit("usage: PS5_HOST=ip ps5fs.py ls|get|rm|quit REMOTE [LOCAL] | head REMOTE BYTES [LOCAL]")
    op, remote = sys.argv[1], sys.argv[2]
    if op == "get":
        cmd_get(host, remote, sys.argv[3] if len(sys.argv) > 3 else os.path.basename(remote))
    elif op == "head":
        cmd_get(host, remote, sys.argv[4] if len(sys.argv) > 4 else os.path.basename(remote), int(sys.argv[3], 0))
    elif op == "ls":
        cmd_text(host, f"LS {remote}")
    elif op == "rm":
        cmd_text(host, f"RM {remote}")
    elif op == "quit":
        cmd_text(host, "QUIT")
    else:
        sys.exit(f"unknown op {op}")


if __name__ == "__main__":
    main()
