# Restore notes

## Writing into the game process

- `kernel_proc_copyin` (mdbg, then DMAP fallback) is not usable for writes: first real write hung the payload in one run and caused a kernel panic (blue light, power off) in another.
- `ptrace(PT_IO, PIOD_WRITE_D)` on the attached game works for all regions, including direct memory (`device`, 812 MiB written, 0 failures, no panic).
- Writing only pages that differ from the dump keeps the freeze short: ~4 MiB CPU memory and ~800 MiB direct memory changed in about a minute of gameplay.

## Same-session restore result (Vice City DE)

- CPU memory only: game crashes (CE-108255-1). Most game state lives in direct memory.
- All writable memory + all thread registers: the frame from the dump is shown again, then the game hangs.

## Why it hangs

Main thread spins in game code waiting for 3 task objects to clear flag `0x4000000` at `+0x10`:

```
1d76e79: mov  rdx, [rax]
         test rdx, rdx
         je   next
         mfence
         mov  rdx, [rdx+0x10]
         mfence
         test edx, 0x4000000
         jne  restart
```

Worker threads sleep in the kernel and never pick the work up. Memory and registers are back in the past, kernel-side state is not:

| libkernel stub | syscall | threads |
|---|---|---|
| `0x80000038c` | 454 `_umtx_op` | TaskGraph, PoolThread, RenderThread, RHIThread |
| `0x8000007aa` | 54 `ioctl` | AgcSubmissionThread (GPU submit) at dump time |
| `0x8000010ec` | 240 `nanosleep` | main thread at dump time, RTHeartBeat |
| `0x80000144c` | 363 `kevent` | AgcInterruptThread, VideoOut |
| `0x800001b2a` | 542 | SceUserServiceEvent, AudioMixerRenderThread |
| `0x800001c8c` | 553 | `[RAGE] netThrPool` |
| `0x80000224c` | 622 | SceShareLibContentE |

State that a memory + register restore does not cover:
- kernel wait queues and Sony kernel objects (semaphores, event flags, equeue registrations and pending events)
- GPU: submitted command buffers, ring read/write pointers, fence/label values written by the GPU
- video out flip queue and its counters

The `PT_ATTACH` stop interrupts sleeping syscalls (EINTR or restart), and the same happens at dump time, so the syscall return path itself is consistent between dump and restore.

## Reference: GPU bits from ps5-linux-loader (13.60)

- GPU device is `/dev/gc`; command submission is `ioctl(fd, 0xC0108102, {u32 pipe_id, u32 count, u64 descriptors})`. This is the `ioctl` AgcSubmissionThread was in at dump time.
- Submit descriptor (16 bytes): `d0 = (gpu_va_lo << 32) | 0xC0023F00` (PM4 INDIRECT_BUFFER), `d1 = ((size_dwords & 0xFFFFF) << 32) | (gpu_va_hi & 0xFFFF)`.
- Per-process GPU address space: `vmspace->vm_vmid` at `0x1EC`, global gvmspace array at `DATA_BASE + 0x3BAA570`, indexed by vmid (holds GPU page directory, start va, size).
- Mapping prot bits: `PROT_GPU_READ 0x10`, `PROT_GPU_WRITE 0x20`.

## GPU drain

Freezing the game leaves GPU work in flight; its end-of-pipe/flip events arrive after the dump and are consumed in the old timeline. Before dumping and before restoring, all threads except `Agc*` (not `AgcSubmissionThread`) and `SceVideoOut*` are `PT_SUSPEND`ed and the game runs for 300 ms, so the GPU finishes and those threads consume their events. With this, the restored game renders for a fraction of a second.

## Hang after drain: GPU never goes idle

`AgcSubmissionThread` then blocks in `ioctl(gc_fd, 0xC0108139, {u32 out, u32 flags=1, u64 out_time})`. Kernel handler (dump VA `0xffffffff80929cf9`) calls `0xffffffff80543a70(ctx)`, which sleeps on a condvar in the device softc (`dev + 0x41268`, mutex `dev + 0x41280`) while the per-process GPU context has outstanding work (`ctx + 0x19c != 0`, queue at `ctx + 0x188`). The GPU never completes, the system watchdog kills the game (CE-108255-1).

Likely cause: the restore rewrites every `device` mapping, which includes driver-owned memory mapped into the process (command rings, read/write pointer shadows, kernel fences), not only game data in direct memory. Those must be excluded.

## Kernel structures seen live (13.60)

`struct kqueue` (FreeBSD 11 layout): `+0x00` mtx, `+0x20` refcnt, `+0x28` kq_list, `+0x38` kq_head (pending knotes), `+0x48` kq_count, `+0xc0` knlistsize, `+0xc8` knlist, `+0xd0` knhashmask, `+0xd8` knhash. Game kqueues: fd 15 (AgcInterruptThread), fd 24 (VideoOut); at freeze time both have no pending events. Registered filters: -11, -13, -14 (Sony).
