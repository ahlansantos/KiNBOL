<div align="center">
<h1>KiNBOL</h1>
  
  <h3>this Kernel is Not Based On Linux</h3>

  [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
  ![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey)
  ![Bootloader: Limine](https://img.shields.io/badge/Bootloader-Limine-green)
  ![Status: Active Development](https://img.shields.io/badge/Status-Active_Development-orange)

  <br />

  [Architecture](#architecture) · [Features](#features) · [Building & Running](#building--running) · [Roadmap](#roadmap)

</div>

---

> *"I'm doing a (free) operating system (just a hobby, won't be big and professional like Linux)."*  
> - Linus Torvalds, 1991

**KiNBOL** (formerly **FreeARS**) is an educational operating system developed completely from scratch for the x86_64 architecture. Initially created as a simple framebuffer kernel, it has evolved into a modern UEFI kernel featuring memory management, interrupt handling, storage abstractions, and the foundations for cooperative multitasking.

---

## 🚀 Features

### Kernel and Architecture
- **Long Mode (x86_64)** > Full 64-bit operation.
- **Modern Interrupts** > GDT, TSS, IDT, PIC Remapping, ACPI parsing.
- **Advanced APIC** > Full support for APIC, IOAPIC, and TSC-calibrated LAPIC Timers (IRQ0 finally working!).
- **Memory Management** > Physical Memory Manager (PMM), Virtual Memory Manager (VMM/Paging) stabilized, and Heap Allocator.
- **Task Scheduler** > Cooperative multitasking foundation (`task_create`, `sched_yield`, `task_exit`) with strict SysV ABI alignment and context isolation.

### Storage & Filesystems
- **VFS** > Virtual File System abstraction.
- **Ramdisk** > In-memory temporary storage.

### Graphics & Display
- **BareGL & Software Renderer** > 1080p boot resolution, scalable font cell (8x16 up to 64x128).
- **Terminal** > Unified keyboard and shell input path, fixed blinking cursor, and PS/2 buffer flushing.

### Shell & Applications (kinSH 2)
| Category | Commands / Tools |
|----------|--------------|
| **System** | `ps`, `dmesg`, `uname`, `ticks`, `date`, `sleep`, `reboot`, `shutdown`, `fastfetch` |
| **Memory** | `meminfo`, `memtest`, `vminfo`, `hexdump`, `peek`, `poke` |
| **Filesystem** | `ramls`, `ramcat`, `ramwrite`, `ramdel`, `vfsls`, `vfsread`, `vfswrite` |
| **Graphics** | `drawtest`, `clearfb`, `scale`, `pixel`, `line`, `rect`, `circle` |
| **Utilities** | `calc`, `ascii`, `anim` |

> [!NOTE]
> Tab-completion has been temporarily removed in 0.07 to simplify the input loop. It will return in a future update once properly refactored.

---

## 🏗️ Building & Running

KiNBOL uses **Limine** for UEFI boot. You can run it on QEMU, VirtualBox, or Real Hardware.

### Prerequisites
- `make`, `cc` (target: x86_64-elf), `nasm`, `ld`
- `qemu-system-x86_64` (for emulation)
- `xorriso`, `mtools` (for ISO/HDD creation)

### Quick Start
```bash
# Clone the repository
git clone https://github.com/your-username/KiNBOL.git
cd KiNBOL

# Build the kernel and run in QEMU
make
make run
```

> [!WARNING]
> If you experience triple faults or crashes related to CR3 or NULL pointers, ensure you are running the latest commit, which fixes a critical virtual-to-physical address translation bug in the scheduler.

---

## 🗺️ Roadmap

- [x] Paging & Virtual Memory Manager
- [x] ACPI, APIC, and IOAPIC
- [x] Cooperative Scheduler Foundation
- [ ] Task Exit Garbage Collection (Reaper)
- [ ] Multitasking & Preemption (LAPIC Timer)
- [ ] Syscalls
- [ ] Ring 3 (User Mode)
- [ ] ELF Loader
- [ ] FAT32 Support
- [ ] AHCI / SATA Drivers

---

## 📖 History & Philosophy

**KiNBOL** is the modern successor to **FreeARS**, a project that originally started as a basic 32-bit kernel and later saw an early 64-bit prototype booted via GRUB. (You can find it on the 32bit-legacy branch!) FreeARS served as a foundational learning ground but has since been officially deprecated and archived. KiNBOL represents a complete architectural reboot, applying those lessons to build a cleaner, modular x86_64 UEFI system using Limine.

KiNBOL exists purely as a learning project. Every subsystem is written from scratch to better understand how modern operating systems actually work.

> [!IMPORTANT]
> The transition to 0.07 marks the biggest milestone in KiNBOL's history. The legacy PIC/PIT was entirely replaced with modern APIC/IOAPIC infrastructure after months of IRQ0 hijacking issues from OVMF/HPET. 

*PS: Being fr, Claude.ai helped me build the APIC, LAPIC, and IOAPIC. Sorry guys, I surrendered to AI.*

---

## ⚖️ License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for details.

> [!IMPORTANT]
> You can do whatever you want with this code, as long as you include the original copyright notice and give credit. It's a hobby project, enjoy!