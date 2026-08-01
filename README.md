# KiNBOL

> **this Kernel is Not Based On Linux**

x86_64 · UEFI · Limine · Ring 0 + Ring 3

---

## 0.07.1 cmd test (outdated)
<img src="pictures/KINBOL-0.07.1-LTS-dump2.png" alt="KiNBOL 0.08 screenshot" width="800"/>

## 0.07.1 gpipe (outdated)
<img src="pictures/KINBOL-0.07.1-LTS-dump3.png" alt="KiNBOL 0.08 screenshot" width="800"/>

*be advised: this screenshot may not correspond to the newest commit. things move fast around here.*

---

## what is this

KiNBOL is a hobby OS I'm building from scratch to learn how operating systems actually work. It started as a simple framebuffer kernel and grew into something with memory management, interrupts, a scheduler, syscalls, and user mode.

Most things here are written from scratch, and a lot of documentation.

---

## current state

| subsystem | status |
|---|---|
| UEFI boot (Limine) | ✅ 64-bit long mode |
| GDT + TSS | ✅ user segments + RSP0 per-task |
| IDT + exceptions | ✅ page fault, GP, etc. + RFLAGS/CS dump |
| PIC → LAPIC/IOAPIC | ✅ PIC disabled, IOAPIC active |
| ACPI (poweroff) | ✅ S5 shutdown |
| PMM (freelist) | ✅ dynamic HHDM |
| VMM (4-level paging) | ✅ pagemap created |
| Heap (first-fit + coalesce) | ✅ 16-byte aligned |
| Spinlock / Big Kernel Lock | ✅ lock xchg + BKL, active since `sched_init()` |
| Scheduler | ✅ cooperative + intelligent preemption (user tasks preempted, kernel/shell protected) |
| Ring 3 + syscalls | ✅ `syscall/sysret` (Linux ABI), `SYS_WRITE`/`SYS_EXIT`/`SYS_READ`/`SYS_SLEEP`/`SYS_YIELD` & more, user pointer + RSP validation, `usertest` shell cmd |
| VFS + ramdisk | ✅ /dev nodes + in-memory fs |
| AHCI + FAT32 | ✅ PCI enum + bus mastering, real read/write DMA, `/dev/sda` mounted as FAT32 (read+write, subdirectories) |
| framebuffer (1080p) | ✅ text terminal + GPipe 1.0 unified — both draw into the same back buffer, only `gpipe_flip`/`gpipe_flip_full` touches real VRAM |
| shell | ✅ commands + history + tab completion (subcommands) |

> scheduling is cooperative with intelligent preemption: tasks give up the CPU voluntarily (`sleep_ms`, `task_exit`, explicit yield). the LAPIC timer fires at 100Hz and hooks into the scheduler, but **kernel tasks and the shell are protected from preemption** (tasks with names starting with `[` are never preempted). this means the terminal/keyboard remain responsive at all times, while user-mode tasks (`usertest`, `schedtest`, etc.) are preempted automatically. **this matters for ring 3**: a user-mode program is CPL-isolated from crashing the kernel, and now also can't hang it indefinitely — the scheduler will preempt spinning userspace tasks. the Big Kernel Lock provides the locking foundation for this, preventing context switches mid-operation on unprotected shared state.
>
> every syscall entry validates the user-supplied pointers it's handed (`syscall_check_user_ptr()`, backed by `vmm_check_user_range()` walking the 4-level page tables). we also validate the RSP the task trapped in with (`syscall_check_user_rsp()`). the RSP check exists because if a task forges a garbage RSP before invoking `syscall`, nothing stops the kernel from handing it straight back on the way out via `sysret`, which corrompts the task's own return into userspace. a task that fails either check gets killed via `task_exit()` instead of being allowed to run the syscall.
>
> ring 3 tasks now have full process isolation via per-task address spaces! `task_create_user()` clones a new pagemap (`vmm_create_pagemap()`) for every user task, and `CR3` context switches automatically inside the scheduler. This guarantees that one user program cannot read or write another user program's memory.

---

## building

```bash
make            # build the ISO
make TOOLCHAIN=llvm run        # build + run in QEMU (UEFI)
```

needs: `make`, `x86_64-elf-gcc`, `nasm`, `qemu-system-x86_64`, `xorriso`, `mtools`

> [DEPRECATED WONT WORK] macOS with HVF acceleration: `make run-hvf`

---

## shell commands

| category | commands |
|---|---|
| system | `ps`, `dmesg`, `dmesg --clear`, `uname`, `ticks`, `date`, `sleep`, `reboot`, `shutdown`, `fastfetch`, `help`, `clear`, `syscalls`, `libktest` |
| memory | `meminfo`, `memtest`, `vminfo`, `hexdump`, `peek`, `poke` |
| filesystem | `ramls`, `ramcat`, `ramwrite`, `ramdel` (ramdisk only) · `ls`, `ls -a`, `cat <file>`, `vfswrite <file> <data>`, `touch <file>`, `mkdir <dir>` (VFS nodes; `ls` shows the disk as `sda` with all FAT32 files/dirs listed as `sda/name`, `touch`/`mkdir` write into the root directory of `/dev/sda` only) |
| graphics | `clearfb`, `scale`, `gpipe`, `gpipe clearfb`, `gpipe drawtest` |
| scheduler | `schedtest`, `sleeptest`, `top` |
| ring 3 | `usertest` — spawns a task, enters ring 3 via `iretq`, runs a hand-written user blob that calls `SYS_WRITE`/`SYS_SLEEP`/`SYS_EXIT` through the modern `syscall` instruction (Linux ABI) |
| debug | `crash de`, `crash ud`, `crash pf`, `crash gp` — deterministic faults for exercising the exception dump (no UB; `crash gp` triggers via `wrmsr`, run from ring 0) |
| utilities | `calc`, `ascii`, `anim`, `mstat` |

---

## GPipe replaces BareGL

BareGL is fully deprecated, moved to `src/graphics/api/deprecated-legacy/`. GPipe 1.0 is the active graphics API:

- explicit context (`gpipe_ctx_t`) instead of hidden global state
- back buffer allocated straight from the PMM (`pmm_alloc_pages_contiguous`), not the small-block heap
- dirty-rect flip — `gpipe_flip()` only pushes the region that changed, `gpipe_flip_full()` forces the whole frame
- full clipping on every primitive (rect/circle/line/bmp)

test it: `gpipe`, `gpipe clearfb`, `gpipe drawtest`

**terminal + GPipe are unified.** `gpipe_get_draw_target()` is the single choke point: `chr()`, `scroll()`, `terminal_clear()`, and `terminal_cursor_draw()` all draw into GPipe's back buffer (falling back to the raw fb only during early boot, before `gpipe_init()` has run), then flip. `gpipe_flip()`/`gpipe_flip_full()` are the only code that ever writes real VRAM. No more stale/ghost pixels when text and gpipe drawing land on the same frame.

**if you're hacking on graphics, use the terminal framebuffer directly. baregl will either get fixed or removed in a future version.**

---

## disk: AHCI + FAT32

`pci_init()` enumerates the bus, finds the AHCI controller (class 01h/06h/01h), enables
**memory space + bus mastering** on it (`pci_enable_device()` — required for the ABAR to
reliably respond and for the HBA to DMA into RAM; firmware often leaves this on already,
which is why it "worked" before, but it isn't guaranteed), then hands off to `ahci_init()`.

`ahci_init()` does a BIOS/OS handoff if the controller supports it, sets `GHC.AE`, finds
the first active SATA port, and rebases it (fresh, zeroed command list + FIS receive area,
32 command slots). It registers `/dev/sda` as a VFS block device backed by real
`ahci_read`/`ahci_write` DMA transfers — both directions now, not just read. All HBA/port
register access is serialized behind a spinlock (`ahci_lock`), since it's shared hardware
state the scheduler could otherwise interleave onto.

`fat32_detect()` peeks at sector 0's BPB (`root_entries == 0 && fat_size16 == 0 &&
fat_size32 != 0`) at boot; if it looks like FAT32, `fat32_init()` mounts it. There is no
FAT12/16 fallback anymore — every image this kernel builds/ships is FAT32, and the old
FAT12 driver was removed. If the check fails, `/dev/sda` is simply left unmounted (the
raw block device is still reachable, just no filesystem on top).

FAT32 support (`fs/fat32.c`) includes:
- full read **and write**, including growing a file across newly-allocated clusters
- **subdirectories** — `fat32_scan_dir()` is the same function for root and every nested
  folder; since this kernel's VFS is a flat namespace (linear array, `strcmp` lookup, no
  path walking), nested files are registered as `parent/child.txt` style flattened names,
  so `cat`/`vfswrite`/`ls` all keep working with zero shell changes
- cluster allocation via a linear FAT scan with a "next free" hint (no FSInfo yet)
- FAT entries are read/written directly from disk per-access rather than cached whole in
  RAM — simpler and always-consistent, at the cost of raw throughput
- short (8.3) names only; long file name (VFAT) entries are recognized and skipped, not
  parsed — a file's long name won't show up, only its short name

**what's still missing, if you want to push this further:** long file names, an FSInfo-based
allocator (avoids rescanning the FAT from scratch when the hint wraps), directory creation
(`mkdir`/new file creation only work in the root directory right now — `touch`/`mkdir` inside
a subfolder isn't implemented yet), and multi-disk / multi-port AHCI support (`active_port`
is a single global).

### `ls` output

`ls` looks like a real Unix `ls`: a plain columnar grid, colored by type, no headers or
`[tag]` labels. Low-level char devices (`null`, `zero`, `random`, `tty`, `dmesg`) and the
internal ramdisk block device (`ram0`) are hidden by default — they're kernel plumbing, not
something a user needs to see every time. `sda` (the disk) is shown bare; everything mounted
off its FAT32 filesystem — files and directories, at any depth — is shown prefixed `sda/`,
e.g. `sda/pasta1/`, `sda/teste.txt`, matching how you'd expect a real disk to be addressed.

`cat`/`vfswrite`/`touch` all accept that `sda/` prefix too (it's stripped in `vfs_find()`
before lookup), so `cat sda/teste.txt` works the same as `cat teste.txt`. `ls -a` also shows
macOS-generated metadata junk (`.fseventsd`, `.Trashes`, `.DS_Store`, AppleDouble `._file`
entries) that gets auto-created any time you mount the disk image on macOS to drop a file
in — hidden by default since it isn't real data.

Filename lookup (`vfs_find`) is case-insensitive, so `cat teste.txt` finds a file the FAT32
short-name table stored as `TESTE.TXT` (short 8.3 names are always uppercase on disk).

To wipe `data.img` clean between test runs without reformatting:
```bash
hdiutil attach data.img            # note the /Volumes/NO NAME mount point
rm -rf "/Volumes/NO NAME"/*
hdiutil detach /dev/diskN
```

### setting up `data.img`

The FAT32 image shipped in this repo starts out **unformatted (all zero bytes)** — you need
to format it once before the kernel will find a valid BPB and mount it:

```bash
# macOS
hdiutil attach -nomount data.img          # note the /dev/diskN it prints
sudo newfs_msdos -F 32 /dev/rdiskN        # use rdisk (raw), not disk, and sudo
hdiutil detach /dev/diskN

# Linux
mkfs.fat -F 32 data.img
```

To drop files onto it before boot: mount the image (`hdiutil attach data.img` on macOS,
loopback-mount on Linux), copy files in, then unmount before starting QEMU.

---

## recent fixes (AHCI/FAT32 debugging session)

- **LBA48 disk size bug** — `ahci_init()` was assembling the IDENTIFY words for LBA48 out
  of order (`id[101]`/`id[100]` swapped with `id[103]`/`id[102]`), producing a bogus 64-bit
  value that truncated to `0` when narrowed to `dev_sda.size`'s `uint32_t`. `/dev/sda`
  reported 0 MB even on a correctly-sized disk. Fixed the word ordering to match the ATA
  spec (words 100–103 are sequential little-endian, low→high).
- **README had stale command names** — docs referenced `vfsls`/`vfsread` which never
  existed in `shell.c`; the real commands are `ls` (no args) and `cat <file>`.
- **`ls` UX overhaul** — output now splits `Devices` vs `Disk Files`, drops the misleading
  fake `/dev/` prefix, and hides macOS-generated FAT metadata junk by default (`ls -a` to
  show it). `ls <anything>` now gives a clear warning instead of falling through to a red
  "command not found".
- **Case-insensitive file lookup** — `vfs_find()` was exact-match only, so a file the FAT32
  driver stored as `TESTE.TXT` couldn't be opened with `cat teste.txt`. Now case-insensitive.
- **FAT32 file/directory creation** — `fat32_create_file()` and `fat32_mkdir()` added
  (`touch`/`mkdir` shell commands), root directory only for now. `mkdir` registers the new
  directory in the VFS immediately, no reboot needed to see it.
- **Directories are now first-class VFS nodes** — `fat32_scan_dir()` used to only recurse
  into subdirectories without registering them; now every directory gets a `VFS_DIRECTORY`
  node too, so `ls` can actually show them.
- **`ls` rewritten to look like a real Unix `ls`** — plain columnar grid instead of boxed
  headers/tags. Low-level char devices (`null`/`zero`/`random`/`tty`/`dmesg`) and the
  internal `ram0` ramdisk are hidden by default (kernel plumbing, not user-facing). The disk
  shows as bare `sda`; everything on its FAT32 filesystem is shown as `sda/name`, and that
  same `sda/` prefix is now accepted by `cat`/`vfswrite`/`touch` for consistency.
- **Isolated-pagemap freeze fix** — `vmm_create_pagemap()` only clones the kernel's PML4
  entries (256–511) at the moment a user task is created; any kernel PML4 entry allocated
  *after* that point (e.g. a new top-level region) was invisible to that task and could
  fault-loop/hang it. Added `vmm_sync_kernel_entry()`, called from the `#PF` handler, which
  copies the missing entry over on demand and retries — the standard "vmalloc fault" pattern.

---

## Linux userland compat — the plan

Next big milestone: run real static binaries (musl-libc) in ring 3. Broken into stages:

1. **ELF loader** (not started) — parse the ELF header, map every `PT_LOAD` segment via
   `vmm_map` at the addresses the header requests, zero the BSS tail, jump to `e_entry` via
   the existing `enter_userspace()`.
2. **Linux-style initial user stack** — musl's `_start` expects `argc, argv[], NULL, envp[],
   NULL, auxv[]..., AT_NULL` already laid out on the stack when it starts running, not just a
   bare entry jump. This has to be built by whatever launches the program (planned shell
   command: `exec <file>`), separate from the ELF parsing itself.
3. **musl toolchain** — musl builds unmodified against `--target=x86_64-linux-musl` (with
   `--disable-shared` for now), since the kernel already speaks the Linux `syscall` ABI —
   no custom target needed, that's the whole point of matching the syscall numbers/registers.
4. **Fill in missing syscalls as they come up** — a musl static binary's `_start` calls a
   handful of syscalls before `main()` even runs (`arch_prctl` for TLS, possibly `brk`,
   `set_tid_address`, `exit_group`). Expect to discover missing ones via the exception dump
   (RAX at the fault = syscall number) and add them incrementally rather than pre-guessing
   the full list.

---

## roadmap

- [x] paging & VMM
- [x] ACPI, APIC, IOAPIC
- [x] cooperative scheduler
- [x] task reaper + sleep
- [x] kernel locks (big-kernel-lock)
- [x] GPipe (BareGL successor): ctx-based, dirty-rect flip, pmm-backed buffer
- [x] syscalls (`syscall`/`sysret`, Linux ABI, `SYS_WRITE`/`SYS_EXIT`)
- [x] ring 3 (`sysret` into CPL=3, per-task kernel/user RSP)
- [x] preemptive scheduling (intelligent: user tasks preempted, kernel/shell protected)
- [x] syscall pointer validation (`syscall_check_user_ptr()` / `vmm_check_user_range()`)
- [x] syscall RSP validation (`syscall_check_user_rsp()`)
- [x] `SYS_READ` (path-based, reads through the VFS), `SYS_SLEEP`, `SYS_YIELD`
- [x] unify terminal + GPipe into one drawing path (`gpipe_get_draw_target()`, single flip choke point)
- [x] per-task address spaces (VMM)!
- [x] PS/2 Mouse driver
- [x] Lib-kin v0.1 (kernel standard library + deduplication)
- [x] userspace memory allocation (`SYS_BRK`, `SYS_MMAP` anonymous)
- [x] POSIX syscall stubs (`open`, `close`, `stat`, `arch_prctl`, etc.)
- [ ] ELF loader (PT_LOAD mapping, BSS zeroing, jump to e_entry)
- [ ] Linux-style initial user stack (argc/argv/envp/auxv) for `exec`
- [ ] musl static toolchain + fill in missing syscalls as discovered
- [x] FAT32 (read + write, subdirectories flattened into VFS, root-dir file/dir creation)
- [x] PCI Enumeration (AHCI/SATA foundation) + memory space / bus mastering enable
- [x] AHCI/SATA driver (single port, read + write DMA, GHC.AE + BOHC handoff, locked HBA access)

---

## license

MIT. do whatever you want, just keep the copyright notice.