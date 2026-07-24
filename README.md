# KiNBOL

> **this Kernel is Not Based On Linux**

x86_64 · UEFI · Limine · Ring 0 + Ring 3

---

## 0.07.1-LTS cmd test
<img src="pictures/KINBOL-0.07.1-LTS-dump2.png" alt="KiNBOL 0.07.1 (LTS commit) screenshot" width="800"/>

## 0.07.1-LTS gpipe
<img src="pictures/KINBOL-0.07.1-LTS-dump3.png" alt="KiNBOL 0.07.1 (LTS commit) screenshot" width="800"/>

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
| Ring 3 + syscalls | ✅ `int 0x80`, `SYS_WRITE`/`SYS_EXIT`, `usertest` shell cmd |
| VFS + ramdisk | ✅ /dev nodes + in-memory fs |
| framebuffer (1080p) | ✅ text terminal (direct fb) + GPipe 1.0 (back buffer, dirty-rect) |
| shell | ✅ commands + history + tab completion (subcommands) |

> scheduling is cooperative with intelligent preemption: tasks give up the CPU voluntarily (`sleep_ms`, `task_exit`, explicit yield). the LAPIC timer fires at 100Hz and hooks into the scheduler, but **kernel tasks and the shell are protected from preemption** (tasks with names starting with `[` are never preempted). this means the terminal/keyboard remain responsive at all times, while user-mode tasks (`usertest`, `schedtest`, etc.) are preempted automatically. **this matters for ring 3**: a user-mode program is CPL-isolated from crashing the kernel, and now also can't hang it indefinitely — the scheduler will preempt spinning userspace tasks. the Big Kernel Lock provides the locking foundation for this, preventing context switches mid-operation on unprotected shared state.
>
> ring 3 is also single-address-space for now: every task shares the same page tables (`vmm_create_pagemap()`/`vmm_destroy_pagemap()` exist but aren't wired into `task_create()` yet), and syscalls don't validate user-supplied pointers. CPL enforcement stops privileged instructions, not memory access — real process isolation needs per-task address spaces first.

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
| system | `ps`, `dmesg`, `dmesg --clear`, `uname`, `ticks`, `date`, `sleep`, `reboot`, `shutdown`, `fastfetch`, `help`, `clear` |
| memory | `meminfo`, `memtest`, `vminfo`, `hexdump`, `peek`, `poke` |
| filesystem | `ramls`, `ramcat`, `ramwrite`, `ramdel`, `vfsls`, `vfsread`, `vfswrite` |
| graphics | `clearfb`, `scale`, `gpipe`, `gpipe clearfb`, `gpipe drawtest` |
| scheduler | `schedtest`, `sleeptest`, `top` |
| ring 3 | `usertest` — spawns a task, enters ring 3 via `iretq`, runs a hand-written user blob that calls `SYS_WRITE`/`SYS_EXIT` through `int 0x80` |
| debug | `crash de`, `crash ud`, `crash pf`, `crash gp` — deterministic faults for exercising the exception dump (no UB; `crash gp` triggers via `wrmsr`, run from ring 0) |
| utilities | `calc`, `ascii`, `anim` |

---

## GPipe replaces BareGL

BareGL is fully deprecated, moved to `src/graphics/api/deprecated-legacy/`. GPipe 1.0 is the active graphics API:

- explicit context (`gpipe_ctx_t`) instead of hidden global state
- back buffer allocated straight from the PMM (`pmm_alloc_pages_contiguous`), not the small-block heap
- dirty-rect flip — `gpipe_flip()` only pushes the region that changed, `gpipe_flip_full()` forces the whole frame
- full clipping on every primitive (rect/circle/line/bmp)

test it: `gpipe`, `gpipe clearfb`, `gpipe drawtest`

**known gotcha:** the text terminal writes straight to the real framebuffer, gpipe draws into its own back buffer. they don't know about each other. call `gpipe_sync_from_fb()` before drawing over existing terminal content, or you'll flip stale (usually black) pixels on top of it.

**if you're hacking on graphics, use the terminal framebuffer directly. baregl will either get fixed or removed in a future version.**

---

## roadmap

- [x] paging & VMM
- [x] ACPI, APIC, IOAPIC
- [x] cooperative scheduler
- [x] task reaper + sleep
- [x] kernel locks (big-kernel-lock)
- [x] GPipe (BareGL successor): ctx-based, dirty-rect flip, pmm-backed buffer
- [x] syscalls (`int 0x80`, DPL=3 gate, `SYS_WRITE`/`SYS_EXIT`)
- [x] ring 3 (`iretq` into CPL=3, per-task RSP0 via TSS)
- [x] preemptive scheduling (intelligent: user tasks preempted, kernel/shell protected)
- [ ] unify terminal + GPipe into one drawing path (right now they're two independent writers to the same fb)
- [ ] per-task address spaces (`vmm_create_pagemap` exists, not wired to tasks yet)
- [ ] syscall pointer validation (user buffers are trusted right now)
- [ ] ELF loader
- [ ] FAT32
- [ ] AHCI/SATA

---

## license

MIT. do whatever you want, just keep the copyright notice.


i really dont know what happened, someone changed the readme and made it portuguese