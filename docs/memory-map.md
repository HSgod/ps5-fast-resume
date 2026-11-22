# Game process memory map

Captured with `payloads/vmmap` on FW 13.60, kstuff + ShadowMountPlus loaded.

Game: GTA Vice City DE, `PPSA03530`, `eboot.bin`, pid 99, RSS ~1913-1938 MiB.

## KERN_PROC_VMMAP on PS5

- Works for another process from an elfldr payload, no kernel writes needed.
- Records are 144 bytes (packed `kve_path`), field layout matches FreeBSD 12 `struct kinfo_vmentry` up to `kve_path` at 0x88.
- `kve_resident` is in 16 KiB pages (`PAGE_SIZE` = 0x4000).
- Paths are empty, Sony mapping names are not exposed here.
- `kve_private_resident` is per object, not per entry; not usable for totals.

## Breakdown (3065 entries)

| type    | prot | n    | virt MiB | resident MiB |
|---------|------|------|---------:|-------------:|
| default | ---  | 291  | 522631.3 | 0            |
| device  | rw-  | 2074 | 1736.0   | 0 (not reported) |
| t9      | --x  | 65   | 84.8     | 84.8         |
| default | rw-  | 339  | 82.5     | 82.5         |
| t9      | r--  | 70   | 24.1     | 24.1         |
| default | r--  | 67   | 8.3      | 8.3          |
| t9      | rw-  | 70   | 1.5      | 1.5          |
| none    | ---  | 87   | 1.4      | 0            |
| t11     | rw-  | 1    | 0.1      | 0.1          |
| phys    | r-x  | 1    | 0        | 0            |

Accessible (non `---`) total: 1937 MiB, matching RSS.

## Interpretation

- `default ---`: reserved address space, nothing to save.
- `t9` (Sony type 9): loaded modules (eboot + sprx). Code and rodata can be reloaded from files; the rw part (1.5 MiB) is module data and must be saved.
- `default rw-`: heap, stacks, TLS, 82.5 MiB, fully resident.
- `device rw-`: direct memory mappings, 1736 MiB in 2074 entries (mostly 64 KiB), range `0xfe0000000`-`0x9000218000`. Residency is not reported, size is the real cost. Shared CPU/GPU memory: GPU resources live here.

Writable state to dump: ~1820 MiB (default rw + t9 rw + device rw).
