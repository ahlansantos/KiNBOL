# KiNBOL
> **this Kernel is Not Based On Linux**

> *"I'm doing a (free) operating system (just a hobby, won't be big and professional like Linux)."*
>
> — Linus Torvalds, 1991

A hobby x86_64 operating system written completely from scratch.

<p align="left">
<img src="https://img.shields.io/badge/version-0.07-blue">
<img src="https://img.shields.io/badge/x86__64-Architecture-success">
<img src="https://img.shields.io/badge/UEFI-Limine-green">
<img src="https://img.shields.io/badge/status-Active_Development-orange">
<img src="https://img.shields.io/badge/language-C11-blue">
</p>

KiNBOL (formerly **FreeARS**) is an educational operating system developed from scratch for the x86_64 architecture.

Initially created as a simple framebuffer kernel, it has evolved into a modern UEFI kernel featuring memory management, interrupt handling, storage abstractions and the foundations for multitasking.

## Current Status

| Version | 0.07 |
|---------|---------|
| Architecture | x86_64 |
| Boot | UEFI + Limine |
| Branch | `x86_64-uefi` |
| State | Active Development |

## Highlights

- UEFI Boot
- x86_64 Long Mode
- GDT + TSS
- IDT
- PIC Remapping
- APIC
- IOAPIC
- LAPIC Timer
- IRQ0 Timer Interrupts
- Paging / Virtual Memory Manager
- Physical Memory Manager
- Heap Allocator
- VFS
- Ramdisk
- RTC
- Shell
- Software Renderer
- Raycasting Demo

## Screenshots

### Raycasting (Removed, as of 0.07 - You can still implement it by yourself tho)

![KiNBOL](pictures/raycast.png)

### Fastfetch

![KiNBOL](pictures/KiNBOL-0.07-dump.png)

## What's New - 0.07

### Interrupt Subsystem

- GDT + TSS
- IDT
- PIC Remapping → full 8259 disable (IMCR)
- ACPI/MADT parsing
- APIC
- IOAPIC
- LAPIC Timer (TSC-calibrated)
- IRQ0 finally working 🎉

This is the biggest milestone in KiNBOL's history so far.

For months, **IRQ0 simply refused to work**. Turned out OVMF/HPET in legacy
replacement mode was hijacking it via the I/O APIC. Rather than patch
around that, the entire timer path was rebuilt on modern APIC/IOAPIC
infrastructure instead of legacy PIC/PIT — the fix that finally stuck.

### Paging / VMM Stabilized

- `vmm_init()` now actually loads the kernel page table into CR3
- MMIO regions (LAPIC/IOAPIC) mapped correctly into the live pagemap
  instead of causing a double-translation bug

### Debuggability

- Panics now dump all 15 general-purpose registers, not just RIP + error
  code + CR2
- 32-entry exception name table — a panic reads `vector 13 - #GP General
  Protection` instead of a bare number
- Fixed a stack-corruption bug in the IDT exception stubs where an extra
  dummy error code was pushed on top of the CPU's real one for
  error-code vectors (#DF/#GP/#PF), shifting every field the dispatcher
  read afterward

### Misc

- Fixed RAM display truncating to a flat GB instead of showing one
  decimal place
- 1080p boot resolution

This unlocks:

- Scheduler
- Multitasking
- Time slicing
- Accurate sleep()
- System clock
- Better hardware support

## Existing Features

### Boot

- UEFI
- Limine

### CPU

- CPUID
- GDT
- TSS
- IDT
- Exceptions
- Interrupts
- APIC
- IOAPIC
- LAPIC Timer

### Memory

- Paging / VMM
- PMM
- Heap
- Heap statistics
- Memory debugger

### Storage

- VFS
- Ramdisk

### Drivers

- PS/2 Keyboard
- RTC
- Serial
- Framebuffer

### Shell

- Fastfetch
- Calculator
- dmesg
- Memory tools
- 20+ commands

## Roadmap

- [x] Paging
- [x] Virtual Memory Manager
- [ ] Scheduler
- [ ] Multitasking
- [ ] Syscalls
- [ ] Ring 3
- [ ] ELF Loader
- [ ] FAT32
- [ ] AHCI

## Version History

| Version | Description |
|----------|-------------|
| 0.01 | First framebuffer kernel |
| 0.02 | 64-bit mode |
| 0.03 | Framebuffer improvements |
| 0.04 | UEFI + Limine |
| 0.05 | PMM + Heap |
| 0.06 | Shell + Drivers |
| 0.06.1 | VFS + Ramdisk + dmesg |
| **0.07** | **Modern interrupt subsystem (GDT, TSS, IDT, ACPI, APIC, IOAPIC, LAPIC, IRQ0), paging/VMM stabilized, full register dump + named exceptions on panic, RAM display fix, 1080p boot resolution** |

## Philosophy

KiNBOL exists purely as a learning project.

Every subsystem is written from scratch to better understand how modern operating systems actually work.
Being fr, Claude.ai helped me build the APIC, LAPIC and IOAPIC. Sorry guys, I surrendered to AI.

## Tested On

- ✅ QEMU
- ✅ VirtualBox
- ✅ Real Hardware

## License

Do whatever you want.

It's a hobby.