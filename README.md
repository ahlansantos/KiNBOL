# KiNBOL

> **this Kernel is Not Based On Linux**

x86_64 · UEFI · Limine · Ring 0 + Ring 3

---

## Sobre

Kernel hobby escrito do zero para aprender como OSes funcionam. Inclui gerenciamento de memória, interrupts, scheduler, syscalls e user mode.

## Estado Atual

- ✅ Boot UEFI 64-bit
- ✅ GDT/TSS com RSP0 por task
- ✅ IDT + exceções
- ✅ LAPIC/IOAPIC + ACPI (poweroff + reboot)
- ✅ PMM + VMM + Heap
- ✅ Scheduler cooperativo + preempção inteligente
- ✅ Ring 3 + syscalls (`int 0x80`)
- ✅ VFS + ramdisk
- ✅ Terminal + GPipe 1.0 (rect, circle, line, text)
- ✅ Shell com history + tab completion

## Build

```bash
make                    # build ISO
make TOOLCHAIN=llvm run # build + QEMU
```

## Comandos

- **Sistema:** `help`, `clear`, `uname`, `ticks`, `date`, `sleep`, `reboot`, `shutdown`
- **Memória:** `meminfo`, `memtest`, `vminfo`, `hexdump`, `peek`, `poke`
- **FS:** `ramls`, `ramcat`, `ramwrite`, `ramdel`, `vfsls`
- **Scheduler:** `schedtest`, `sleeptest`, `top`, `usertest`
- **Gráficos:** `gpipe`, `gpipe drawtest`, `clearfb`, `scale`
- **Debug:** `crash de/ud/pf/gp`, `dmesg`

## Roadmap

- [x] Preempção inteligente (user tasks preempted, kernel/shell protected)
- [x] GPipe primitives (rect, circle, line, text)
- [x] ACPI reboot
- [ ] Per-task address spaces
- [ ] Syscall pointer validation
- [ ] ELF loader
- [ ] FAT32 + AHCI/SATA

## License

MIT