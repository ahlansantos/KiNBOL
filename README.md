# KiNBOL

**this Kernel is Not Based On Linux**

x86_64 · UEFI · Limine · Ring 0 only

---

## What is this

KiNBOL is a hobby OS I'm building from scratch to learn how operating systems actually work. It started as a simple framebuffer kernel and grew into something with memory management, interrupts, a scheduler, and a shell.

Most things here are written from scratch, and a lot of documentation.

---

## Current state

- 64-bit long mode, UEFI boot via Limine
- GDT, TSS, IDT, APIC, IOAPIC, ACPI
- Physical + virtual memory management
- Cooperative scheduler with task sleep/wake
- Simple VFS + ramdisk
- Framebuffer graphics (1080p)
- Shell with basic commands

---

## Building

```bash
make
make run
```

Requires: `make`, `x86_64-elf-gcc`, `nasm`, `qemu-system-x86_64`, `xorriso`, `mtools`

---

## Commands

| Category | Commands |
|----------|----------|
| System | `ps`, `dmesg`, `uname`, `ticks`, `date`, `sleep`, `reboot`, `shutdown`, `fastfetch` |
| Memory | `meminfo`, `memtest`, `vminfo`, `hexdump`, `peek`, `poke` |
| Filesystem | `ramls`, `ramcat`, `ramwrite`, `ramdel`, `vfsls`, `vfsread`, `vfswrite` |
| Graphics | `clearfb`, `scale` |
| Scheduler tests | `schedtest`, `sleeptest` |
| Utilities | `calc`, `ascii`, `anim` |

> Tab-completion is temporarily disabled in 0.07.1. It'll come back.

---

## Graphics note

BareGL is deprecated. Most of its functions are broken or unmaintained. Only `clearfb` and `scale` are safe to use right now. Everything else (`pixel`, `line`, `rect`, `circle`, `drawtest`) is legacy code from 0.05/0.06 and may crash.

---

## Recent changes

- **Dynamic HHDM**: VMM now maps all physical memory (not just 4GB) based on the memory map. Fixes page faults on systems with >4GB RAM.
- **16-byte heap alignment**: kmalloc now returns 16-byte aligned pointers for SysV ABI compliance.
- **16KB task stacks**: Scheduler tasks now get 16KB stacks (4 contiguous pages) instead of 4KB. Prevents stack overflow in deep call chains.
- **BareGL deprecated**: All graphics commands except `clearfb` and `scale` are deprecated. The old BareGL drawing functions (`pixel`, `line`, `rect`, `circle`, `drawtest`) are removed from the shell.

---

## Roadmap

- [x] Paging & VMM
- [x] ACPI, APIC, IOAPIC
- [x] Cooperative scheduler
- [x] Task reaper + sleep
- [ ] Preemption (LAPIC timer)
- [ ] Syscalls
- [ ] Ring 3
- [ ] ELF loader
- [ ] FAT32
- [ ] AHCI/SATA

---

## License

MIT. Do whatever you want, just keep the copyright notice.

---

*PS: Claude.ai helped me build the APIC stuff. I'm not sorry.*
