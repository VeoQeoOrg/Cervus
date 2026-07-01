# Overview

Cervus is a monolithic 64-bit kernel plus a complete Unix-style userland, built
entirely from source in one repository. This page gives a high-level picture of
what exists, how it is layered, and where each piece lives.

## Philosophy

* **No black boxes.** Every layer from `_start` to `cat` is in-tree — roughly
  13k lines of kernel C, plus a clearly factored libc and userland. The full
  stack can be read end-to-end.
* **Not a Linux clone.** Cervus has its own kernel ABI, its own syscall numbers
  and its own libc. It speaks POSIX where reasonable but is not bound to Linux
  internals: no `clone` flags, no glibc ABI, no `/proc` assumptions baked into
  userland.
* **Real userland.** An interactive shell with history, a usable text editor
  (`neo`), a working C compiler (TCC), a real filesystem on a real disk, and an
  installer that produces a bootable system.
* **One-command build.** The build system is a single C binary checked into the
  repository. `./build` produces an ISO and launches QEMU.

## Feature matrix

| Component | Status | Notes |
| :--- | :---: | :--- |
| Bootloader | ✅ Done | Limine, BIOS + UEFI |
| Graphics / PSF font | ✅ Done | Linear framebuffer, software text rendering |
| Memory (PMM / VMM) | ✅ Done | Bitmap + buddy PMM, slab allocator, 4-level paging |
| Interrupts (IDT) | ✅ Done | Exceptions and hardware IRQs |
| ACPI | ◑ Partial | Table parsing / SDT discovery; full reset path pending |
| APIC / IOAPIC / LAPIC | ✅ Done | Per-CPU LAPIC, IOAPIC IRQ routing |
| Timers (HPET / APIC) | ✅ Done | Periodic and one-shot |
| SMP | ✅ Done | Multicore boot, per-CPU state |
| Scheduler | ✅ Done | Preemptive, priority run-queues, `fork`/`exec`/`wait` |
| Userspace | ✅ Done | Ring 3, ~60 syscalls, `libcervus` |
| VFS + ext2 / FAT32 | ✅ Done | Read/write, in-kernel `mkfs` |
| ISO9660 / initramfs | ✅ Done | Live-ISO boot path |
| PCI / PCIe | ✅ Done | MCFG + legacy CF8/CFC, bridge walk, MSI/MSI-X |
| USB | ✅ Done | xHCI / EHCI / UHCI, HID + mass storage |
| Disk installer | ✅ Done | BIOS + UEFI, MBR, ESP + ext2 root + swap |
| On-device C compiler | ✅ Done | TCC ported and bundled |
| Networking | ⬜ Not started | No TCP/IP stack yet |
| GUI | ⬜ Not started | No compositor yet |

## Source tree

```
kernel/                     x86_64 kernel
  include/                  public kernel headers, mirrors src/ layout
  src/
    kernel.c                kernel_main — boot orchestration
    gdt/                    Global Descriptor Table + TSS
    interrupts/             IDT, ISR (exceptions), IRQ dispatch
    memory/                 pmm, vmm, paging, dma
    sse/                    FPU / SSE state save & restore
    acpi/                   ACPI table parsing
    apic/                   LAPIC, IOAPIC, generic APIC
    smp/                    multicore startup, per-CPU areas
    sched/                  scheduler, task lifecycle, context switch (asm)
    syscall/                syscall entry (asm) + one file per syscall
    fs/                     vfs, ext2, fat32, iso9660, ramfs, devfs, procfs, initramfs
    drivers/
      disk/                 ata, ahci, nvme, blkdev, partition (MBR/GPT)
      usb/                  xhci, ehci, uhci + hid/hub/msc + enumeration
      pci.c, ps2.c, timer.c, mouse.c, keymap.c
    graphics/fb/            framebuffer + PSF font rendering
    console/                virtual terminal, kernel log, monitor
    io/                     serial (COM), port I/O helpers
    time/                   clocksource abstraction
    panic/                  kernel panic handler

usr/
  lib/libcervus/            C library — one function per .c, private <libcervus.h>
    unistd/ fcntl/ string/ stdio/ stdlib/ ctype/ time/ math/
    termios/ signal/ dirent/ sys/ cervus/ internal/
    crt0.asm setjmp.asm
  sysroot/usr/include/      public userland headers (installed into the system)
  apps/                     init, shell, neo editor, calc, cal, date, fetch, sysmon
  bin/                      coreutils-style utilities (60+ programs)
  installer/                on-disk installer (Live ISO only)

builder/build.c             single-binary build system (host tool)
wallpapers/                 boot wallpapers (multiple resolutions)
```

Kernel headers under `kernel/include/` mirror the `kernel/src/` directory
structure one-to-one, so the header for `src/apic/lapic.c` lives at
`include/apic/apic.h`, and so on.

## What "written from scratch" covers

The repository contains its own implementations of:

* the physical and virtual memory managers, the page-table walker and a slab
  allocator;
* the interrupt/exception plumbing, ACPI parsing and the APIC stack;
* a preemptive SMP scheduler with `fork`/`exec`/`wait`, pipes and basic signals;
* a VFS with five on-/in-memory filesystems and two on-disk filesystems
  (ext2, FAT32) including in-kernel `mkfs`;
* PCI/PCIe enumeration and USB (three host-controller families);
* a POSIX-style C library (`libcervus`);
* a shell, a text editor, ~60 coreutils and a disk installer.

Third-party code is limited to: the Limine boot protocol headers, the OSDev
`freestnd-c-hdrs` / `cc-runtime` support, and the bundled **TCC** compiler.
These are fetched at build time (see [Getting Started](Getting-Started)).

---

Continue to **[Getting Started](Getting-Started)** to build and boot the system.
