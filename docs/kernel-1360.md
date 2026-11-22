# Kernel 13.60 notes

Derived from a 13.60 kernel dump (text + data, 0x2CC0000 bytes, text first). Offsets below are relative to the dump base `0xffffffff80210000` (text base); data base is text base + `0xCC0000`. Use them only as offsets, the live kernel is randomized.

## Syscall table

- Syscall names (DTrace provider table, 0x18 per entry): text base + `0xdb3bc8`.
- Syscall dispatch table, 0x18 per entry, first qword is a CFI jump-table thunk (`jmp rel32`) to the handler: text base + `0xce07e8`.

| # | name | handler (dump VA) |
|---|---|---|
| 54 | ioctl | `0xffffffff806fcf40` |
| 240 | nanosleep | `0xffffffff808cc4e0` |
| 363 | kevent | `0xffffffff805234e0` |
| 454 | _umtx_op | `0xffffffff8058ec40` |
| 538 | evf_create | `0xffffffff80dbfa80` |
| 542 | evf_wait | `0xffffffff80dbfe50` |
| 544 | evf_set | `0xffffffff80dc0150` |
| 545 | evf_clear | `0xffffffff80dc01d0` |
| 549 | osem_create | `0xffffffff80e26020` |
| 553 | osem_wait | `0xffffffff80e263d0` |
| 555 | osem_post | `0xffffffff80e265d0` |
| 622 | ipmimgr_call | `0xffffffff809ecd40` |

## Per-process Sony object table

`td->td_proc` is `td + 0x8`; `proc + 0xc48` points to the table.

Table:
- `+0x000` array of page pointers, 128 entries per page
- `+0x218` table lock
- `+0x220` u32 number of pages

Lookup (`0xffffffff805a9ee0(table, id, type, &entry)`): `idx = id & 0x1fff`, page `idx >> 7`, entry `idx & 0x7f`; valid when `((gen << 13) & 0xffff) | idx == id`, `state == 3` and type matches (`-1` matches any).

Entry (0x30 bytes):
- `+0x00` entry lock
- `+0x10` object pointer
- `+0x18` owner thread while held
- `+0x20` u16 type: `0x110` event flag, `0x120` semaphore
- `+0x26` u16 state, 3 = in use
- `+0x28` u32 generation

## Event flag object (0x58 bytes, type 0x110)

- `+0x00` u64 bit pattern
- `+0x20` mutex
- `+0x28` condvar
- `+0x38` waiter list (TAILQ head, `+0x40` last)
- `+0x48` u32 attributes
- `+0x4c` u32 number of waiters
- `+0x50` u32 shared flag
- `+0x54` u32 creator pid

## Semaphore object (0x60 bytes, type 0x120)

- `+0x18` mutex
- `+0x20` condvar
- `+0x30` waiter list (TAILQ head, `+0x38` last)
- `+0x40` i32 current count
- `+0x44` u32 attributes
- `+0x48` i32 initial count
- `+0x4c` i32 max count
- `+0x50` u32 number of waiters
- `+0x54` u32 shared flag

## Other

- `proc + 0x10` first thread, `td + 0x10` next thread, `td + 0x9c` tid (from the SDK).
- `proc + 0x48` filedesc; table at `*filedesc`, `nfiles` at `+0`, entries from `+0x8`, 0x30 each, file pointer first (from the SDK).
