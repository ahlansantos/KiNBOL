<p align="center">
  <img src="pictures/kinbol.png" alt="KiNBOL logo" width="220"/>
</p>

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

~~Most things here are written from scratch, and a lot of documentation.~~

---

## current state

| subsystem | status |
|---|---|
| UEFI boot (Limine) | ✅ 64-bit long mode |
| GDT + TSS | ✅ user segments + RSP0 per-task |
| IDT + exceptions | ✅ page fault, GP, etc. + RFLAGS/CS dump + **ring-3 fault isolation** (a userspace fault kills only that task via `task_exit()`; kernel-mode faults still halt the system) |
| PIC → LAPIC/IOAPIC | ✅ PIC disabled, IOAPIC active |
| ACPI (poweroff) | ✅ S5 shutdown |
| PMM (freelist) | ✅ dynamic HHDM |
| VMM (4-level paging) | ✅ pagemap created |
| Heap (first-fit + coalesce) | ✅ 16-byte aligned |
| Spinlock / Big Kernel Lock | ✅ lock xchg + BKL, active since `sched_init()` |
| Scheduler | ✅ cooperative + intelligent preemption (user tasks preempted, kernel/shell protected) |
| Ring 3 + syscalls | ✅ `syscall/sysret` (Linux ABI), `SYS_WRITE`/`SYS_EXIT`/`SYS_READ`/`SYS_SLEEP`/`SYS_YIELD`/`SYS_BRK`/`SYS_MMAP`/`SYS_MPROTECT`/`SYS_MUNMAP`/**`SYS_OPEN`/`SYS_CLOSE`/`SYS_LSEEK`/`SYS_STAT`/`SYS_FSTAT`** (real, fd-table backed — no longer stubbed `-ENOSYS`) & more, user pointer + RSP validation, `usertest` shell cmd |
| ASLR | ✅ PIE load-bias, stack top, and mmap arena base are all randomized per exec (`kernel/rand.c`, xorshift64* seeded from RDSEED/TSC) |
| stack guard page | ✅ every user stack has a deliberately-unmapped page directly below it; a stack overflow takes a clean, immediately-identified `#PF` (`kernel/idt.c` recognizes the guard address) instead of silently corrupting adjacent memory |
| /proc | ✅ minimal read-only `procfs` (`fs/procfs.c`): `/proc/version`, `/proc/cpuinfo`, `/proc/uptime`, `/proc/self/status`, `/proc/self/exe` |
| envp | ✅ `exec` now builds a real (currently fixed: `PATH`/`HOME`/`TERM`/`USER`) environment block on the initial user stack, not just an empty terminator |
| unified kernel logging (`klog`) | ✅ single call site (`KLOG_I`/`KLOG_W`/`KLOG_E`/`KLOG_D`/`KLOG_T`) fans out to the dmesg ring buffer + serial *and* the screen terminal at once, level-tagged and colorized, no more drifting between two separate hand-rolled log paths |
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
| filesystem | `ramls`, `ramcat`, `ramwrite`, `ramdel` (ramdisk only) · `ls`, `ls -a`, `cat <file>`, `vfswrite <file> <data>`, `touch <path>`, `mkdir <path>`, `rm <path>`, `rm -r <path>`, `rmdir <path>` (VFS nodes; `ls` shows the disk as `sda` with all FAT32 files/dirs listed as `sda/name`, `touch`/`mkdir`/`rm`/`rmdir` work at any depth via `sda/pasta1/x.txt`-style paths) |
| graphics | `clearfb`, `scale`, `gpipe`, `gpipe clearfb`, `gpipe drawtest` |
| scheduler | `schedtest`, `sleeptest`, `top` |
| ring 3 | `usertest` — spawns a task, enters ring 3 via `iretq`, runs a hand-written user blob that calls `SYS_WRITE`/`SYS_SLEEP`/`SYS_EXIT` through the modern `syscall` instruction (Linux ABI) · `exec <path>` — loads a real ELF64 binary off the VFS via `elf_load()`, builds a real Linux-shaped initial stack (`argc`/`argv[0]`/auxv), and enters ring 3 at its `e_entry` (`envp` still empty, no dynamic linking) |
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
- cluster allocation via an FSInfo-backed hint (`next_free`/`free_count` read at mount,
  updated on every allocation); falls back to a linear FAT scan from cluster 2 if the
  FSInfo sector's signatures don't validate (older/foreign images)
- directory creation (`touch`/`mkdir`) works at any depth, not just root — both resolve
  the parent directory's cluster through the VFS (every directory node now carries its
  own `start_cluster`), and a directory chain that's completely full gets extended with
  a freshly-allocated cluster automatically instead of failing
- FAT entries are read/written directly from disk per-access rather than cached whole in
  RAM — simpler and always-consistent, at the cost of raw throughput
- **long file names (VFAT/LFN)** — names that don't fit 8.3 (too long, lowercase, multiple
  dots) get real LFN entries on write (UTF-16, checksum, sequence-numbered), with a unique
  short name generated alongside (`SUBFO~1`, `SUBFO~2`, ...) for backward compatibility.
  `fat32_scan_dir()` reconstructs the long name on read by walking the LFN chain backward
  from the short entry and validating its checksum; a corrupt/orphaned LFN chain falls back
  to the short name instead of showing garbage
- **`rm`/`rmdir`, including recursive delete** — `fat32_remove()` frees the cluster chain,
  tombstones the short entry (`0xE5`) on disk, and walks backward to tombstone any LFN
  entries that belonged to it (matched by checksum) so no directory slots leak. `rm -r` on a
  directory finds every VFS node nested under that path, deletes deepest-first, then removes
  the directory itself; a non-recursive `rmdir` refuses unless the directory is actually empty
- directory creation (`touch`/`mkdir`) works at any depth, not just root — both resolve
  the parent directory's cluster through the VFS (every directory node now carries its
  own `start_cluster`), and a directory chain that's completely full gets extended with
  a freshly-allocated cluster automatically instead of failing
- `touch`/`mkdir` reject a path that already exists (`FAT32_ERR_EXISTS`) instead of silently
  creating a second, colliding VFS node for the same name
- all mutating FAT32 entry points (`fat32_create_file`, `fat32_mkdir`, `fat32_remove`) are
  serialized behind their own `fs_lock` spinlock (`kernel/lock.c`) — separate from the
  scheduler's BKL, which only ever protected the task list, not filesystem state

**what's still missing, if you want to push this further:** rename, and multi-disk /
multi-port AHCI support (`active_port` is a single global). LFN entry cleanup on delete
assumes the LFN run for an entry lives in the same cluster as its short entry, which is true
for every name this driver itself writes but may miss orphaned entries on a directory-entry
run that straddles a cluster boundary if written by something else.

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
  (`touch`/`mkdir` shell commands), root directory only at first. `mkdir` registers the new
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
- **FSInfo-backed cluster allocator** — `fat32_init()` now reads the FSInfo sector
  (`bpb->fs_info`), validates its lead/struc/trail signatures, and seeds `next_free_hint`
  from it instead of always starting the scan at cluster 2. `fat32_alloc_cluster()` updates
  `free_count`/`next_free` and writes the sector back on every allocation; falls back to the
  old full-scan behavior if the signatures don't check out.
- **`touch`/`mkdir` work at any depth** — directories now carry their own `start_cluster`
  (same as files), so a path like `pasta1/sub/arquivo.txt` resolves its parent through the
  VFS instead of needing root. A directory chain that's completely full also gets extended
  with a new cluster automatically, instead of failing outright (this used to be a root-only
  limitation, and even root didn't grow when full).
- **`ls` is now a tree** — entries are sorted alphabetically (which naturally groups a
  directory right before its own children) and printed indented by path depth instead of a
  flat column grid; the macOS-junk filter now checks only the leaf name instead of the whole
  path, so a hidden file inside a visible subfolder is filtered correctly.
- **Long file names (VFAT/LFN)** — `mkdir folder1/subfolder1` used to silently store
  `SUBFOLDE` (8.3-truncated, no warning), and a later `touch folder1/subfolder1/x.txt` would
  fail with a misleading "disk full" because the real parent name didn't match. FAT32 write
  paths now emit proper LFN entries when a name doesn't fit 8.3, with a generated-unique
  short name kept alongside for compatibility; the scan path reconstructs the long name from
  the LFN chain (checksum-validated against the short entry) so it round-trips correctly
  across reboots.
- **`rm` / `rm -r` / `rmdir`** — first delete support of any kind; previously the FSInfo free
  counters only ever went down. Frees the cluster chain, tombstones the directory entry (and
  any LFN entries belonging to it) on disk, and unregisters the VFS node. `rm -r` recurses;
  plain `rmdir` refuses on a non-empty directory instead of silently orphaning its contents.
- **`fs_lock`, separate from the scheduler's BKL** — the BKL (`kernel/lock.c`) only ever
  guarded the task list; FAT32 had no locking of its own at all. Rather than overload the
  scheduler's lock (which would serialize filesystem work against scheduling for no reason),
  every mutating FAT32 entry point now takes a dedicated `fs_lock` spinlock.
- **`touch`/`mkdir` now reject existing names** — creating a path that already resolves to a
  VFS node returns `FAT32_ERR_EXISTS` instead of silently registering a second, colliding
  node for the same name (this could previously happen from re-running `touch` on the same
  file, corrupting the flat VFS namespace's assumption that names are unique).

---

## testing the ELF loader

The loader is wired into a real shell command now: `exec <path>`. It spawns a user task
(`exec_launch()` in `kernel/src/kernel/usermode.c`, same shape as `usertest_launch()`), calls
`elf_load()` on the given VFS path, maps a stack, and jumps into ring 3 at `e_entry`.
**Confirmed working end to end** — a minimal freestanding `test.elf` (raw syscalls only, no
libc: `write`, `sleep`, `write`, `exit`) loads, runs, sleeps, wakes, and exits cleanly through
the real scheduler/reaper path.

1. Build a tiny static ELF to load. `userland/test.c` in this repo is the confirmed-working
   example (raw syscalls only, no libc — `PT_INTERP`/dynamic linking isn't supported yet):
   ```bash
   x86_64-elf-gcc -static -nostdlib -no-pie -o test.elf userland/test.c
   ```
   Keep any new test binaries to raw syscalls (`write`/`exit` via inline `syscall` asm, same shape as the
   existing `user_blob` in `usermode.c`) — `exec` now builds a real `argc`/`argv[0]`/auxv stack, but
   `envp` is still an empty terminator and dynamic linking (`PT_INTERP`) isn't supported yet.
2. Copy `test.elf` onto `data.img` with `mtools` (no mounting needed):
   ```bash
   mcopy -i data.img test.elf ::test.elf
   ```
3. `make TOOLCHAIN=llvm run`, then from the shell: `exec sda/test.elf`. Sample `dmesg` output
   from an actual run:
   ```
   [elf] loaded sda/test.elf entry=0x00000000004000CB highest_vaddr=0x0000000000401000
   [exec] pid 2 entering ring 3 at 0x00000000004000CB
   [syscall] pid 2 num=1
   [syscall] pid 2 num=35
   [sched] task 2 blocked until tick 13384
   [sched] task 2 woke up
   [syscall] pid 2 num=1
   [syscall] pid 2 num=60
   [syscall] task exited
   [sched] task 2 ('exec') exited
   [reaper] task 2 destroyed
   [reaper] cleanup complete
   ```
4. Sanity checks worth trying deliberately: a 32-bit ELF (should reject on `ELFCLASS64`), a
   dynamically-linked binary (should reject on `PT_INTERP`), and a binary bigger than
   available physical memory (should fail cleanly via `elf_map_segment`'s OOM path, not
   panic).

---

## testing SYS_MPROTECT / SYS_MUNMAP

Both are now real (`vmm_protect()`/`vmm_unmap_range()` in `kernel/src/mm/vmm.c`), not stubs.
Two test binaries, same build/copy pattern as `test.elf`:

- `test-mprotect.elf` — mmaps a page, writes/reads through it, calls `mprotect(PROT_READ)`,
  confirms it's still readable, confirms an unaligned `mprotect` is rejected cleanly (no crash),
  mmaps a second page and `munmap`s it. All safe checks, exits cleanly with `PASS:` lines.
- `test-crash.elf` — same setup, but *deliberately* writes to the page after `mprotect(PROT_READ)`.
  This is expected to fault. Confirmed behavior:
  ```
  [TRACE] syscall: pid 2 num=1
  [idt] fatal exception vector=14 (#PF Page Fault) err=0x0000000000000007 rip=... rax=0x0000700000000000
  ```
  `err=0x7` = present page (`P=1`), caused by a write (`W=1`), from user mode (`U=1`) — exactly
  the write-protection fault `mprotect(PROT_READ)` is supposed to produce. Confirmed the task dies
  and the kernel/shell keep running (see ring-3 fault isolation above) rather than halting the
  whole system.

---

## ring-3 fault isolation

`exception_fatal()` in `kernel/src/kernel/idt.c` now checks the CPL of the faulting context
(bits 0-1 of the saved `CS`). If the fault came from ring 3, only that task is killed
(`task_exit()`, the same safe from-interrupt-context yield the PIT timer ISR already uses) and
everything else keeps running — matching how a real Linux kernel handles a userspace segfault.
If the fault came from ring 0 (an actual kernel bug, or the `crash de`/`ud`/`pf`/`gp` shell
commands, which run in kernel mode), the system still halts as before — that case really is fatal.

---

Next big milestone: run real static binaries (musl-libc) in ring 3. Broken into stages:

1. **ELF loader** (`kernel/src/loader/elf.h`, `elf_loader.c`) — parses the ELF64 header,
   validates magic/class/endianness/machine, walks every `PT_LOAD` segment and maps it via
   `vmm_map` at the addresses the header requests (page-aligned, `p_filesz`/`p_memsz`
   handled correctly so BSS is zero-filled), rejects `PT_INTERP` for now (no dynamic linker
   yet), supports `ET_DYN`/PIE via a fixed load bias. **W^X is enforced per segment** —
   executable segments never get `VMM_WRITE`, non-executable segments always get `VMM_NX`.
   Wired into the shell via `exec <path>` (`exec_launch()`/`exec_run()` in `usermode.c`).
2. **Linux-style initial user stack** — ✅ done. `build_initial_user_stack()` in `usermode.c` lays
   out a fixed 16-word/128-byte block (`argc`, `argv[0]`, argv/envp terminators, and a real `auxv`
   array — `AT_PHDR`/`AT_PHENT`/`AT_PHNUM`/`AT_ENTRY`/`AT_BASE`/`AT_NULL` — pulled straight from the
   ELF loader's own output), always 16-byte aligned by construction. `envp` is currently just an
   empty terminator (no environment variables passed yet); `argc` is hardcoded to `1` (no extra
   argv from the shell's `exec <path>` yet).
3. **musl toolchain** — musl builds unmodified against `--target=x86_64-linux-musl` (with
   `--disable-shared` for now), since the kernel already speaks the Linux `syscall` ABI —
   no custom target needed, that's the whole point of matching the syscall numbers/registers.
4. **Fill in missing syscalls as they come up** — a musl static binary's `_start` calls a
   handful of syscalls before `main()` even runs (`arch_prctl` for TLS, possibly `brk`,
   `set_tid_address`, `exit_group`). `SYS_MPROTECT`/`SYS_MUNMAP` are now real (not stubs) —
   this matters because musl's `_start` calls `mprotect` early for RELRO on PIE binaries, and
   would previously have died on `-ENOSYS` before reaching `main()`. Expect to discover any
   remaining missing ones via the exception dump (RAX at the fault = syscall number) and add
   them incrementally rather than pre-guessing the full list.

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
- [x] `SYS_READ` (fd-based, backed by a per-task fd table), `SYS_SLEEP`, `SYS_YIELD`
- [x] unify terminal + GPipe into one drawing path (`gpipe_get_draw_target()`, single flip choke point)
- [x] per-task address spaces (VMM)!
- [x] PS/2 Mouse driver
- [x] Lib-kin v0.1 (kernel standard library + deduplication)
- [x] userspace memory allocation (`SYS_BRK`, `SYS_MMAP` anonymous)
- [x] POSIX syscall stubs (`open`, `close`, `stat`, `arch_prctl`, etc.)
- [x] ELF loader — PT_LOAD mapping + W^X per segment, BSS zeroing, ET_DYN/PIE load bias,
      wired into the shell as `exec <path>` (no `PT_INTERP`/dynamic linking yet)
- [x] Linux-style initial user stack (argc/argv[0]/auxv/envp) for `exec` — envp now carries a
      small fixed default environment (`PATH`/`HOME`/`TERM`/`USER`), not just an empty terminator
- [x] real `SYS_MPROTECT`/`SYS_MUNMAP` (`vmm_protect()`/`vmm_unmap_range()` in `vmm.c`) — no
      longer stubbed `-ENOSYS`, needed for musl's RELRO `mprotect` call in `_start`
- [x] real `SYS_OPEN`/`SYS_CLOSE`/`SYS_LSEEK`/`SYS_STAT`/`SYS_FSTAT`, backed by a small per-task
      fd table (`kernel/sched.h`); `SYS_READ` moved from a path-based custom shape to the real
      `read(fd, buf, len)` Linux ABI to match — **breaking ABI change**, old test ELFs built
      against the previous `SYS_READ` need rebuilding against `elfs/kinlibc.h`
- [x] ASLR — PIE load bias, stack top, and mmap arena base are each randomized per `exec`
      (`kernel/rand.c`, xorshift64* seeded from RDSEED-when-available + TSC/PIT)
- [x] stack guard page — the page directly below every user stack is deliberately left
      unmapped; a stack overflow takes an immediately-identified `#PF` (`idt.c` recognizes the
      guard address specifically) instead of silently corrupting adjacent memory
- [x] minimal `/proc` (`fs/procfs.c`): `/proc/version`, `/proc/cpuinfo`, `/proc/uptime`,
      `/proc/self/status`, `/proc/self/exe`
- [x] `elfs/kinlibc.h` — shared userland syscall-wrapper header (open/read/write/close/mmap/
      mprotect/print helpers + the `_start` trampoline) so new test/example ELFs don't hand-roll
      raw `syscall` asm every time
- [x] ring-3 fault isolation — a userspace exception (`#PF`, `#GP`, etc.) now kills only the
      faulting task via `task_exit()`; the kernel and every other task keep running. Kernel-mode
      faults (CPL 0, e.g. `crash pf`) still halt the system as before
- [x] unified kernel logging (`klog.c`/`klog.h`) — one call site fans out to dmesg/serial and
      the screen terminal together, level-tagged and colorized
- [ ] musl static toolchain + fill in any remaining missing syscalls as discovered
- [ ] real per-task `argv`/custom `envp` for `exec <path> arg1 arg2` (currently fixed defaults)
- [ ] unmapped NULL page (guard page for the stack is done; low-address NULL-deref guard is not)
- [ ] syscall allow-list / capability model per task (currently any ring-3 task can call any
      wired syscall)
- [ ] filesystem permission bits (FAT32 has none natively; any task with a valid fd can
      read/write any file — no owner/mode enforcement yet)
- [x] FAT32 (read + write, subdirectories flattened into VFS, file/dir creation and recursive
      deletion at any depth via FSInfo-backed allocator, long file names)
- [x] PCI Enumeration (AHCI/SATA foundation) + memory space / bus mastering enable
- [x] AHCI/SATA driver (single port, read + write DMA, GHC.AE + BOHC handoff, locked HBA access)

---

## license

MIT. do whatever you want, just keep the copyright notice.