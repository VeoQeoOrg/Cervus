<p align="center">
  <img src="https://github.com/VeoQeo/Cervus/blob/main/wallpapers/cervus_logo.jpg" alt="Cervus OS Logo" width="400px">
</p>

<p align="center">
  <a href="https://t.me/veoqeo_off">
    <img src="https://img.shields.io/badge/Telegram-Channel-blue?style=for-the-badge&logo=telegram" alt="Telegram Channel">
  </a>
  <br>
  <sub><b>Follow Cervus OS news on our Telegram channel:</b> <a href="https://t.me/veoqeo_off">t.me/veoqeo_off</a></sub>
</p>


# Cervus x86_64 Operating System

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey.svg)](https://en.wikipedia.org/wiki/X86-64)
[![Stage: Alpha](https://img.shields.io/badge/Stage-Alpha-orange.svg)]()

**Cervus** is a 64-bit operating system written from scratch for the x86_64 architecture. Everything — kernel, C library, userland, installer, shell, text editor — lives in this repository. There is no Linux compatibility layer, no busybox, no external libc.

The goal is a self-hosting, minimal but actually usable Unix-style environment: boot from BIOS or UEFI, install onto a real disk, log in, edit and compile C code on the machine itself.

## What works today

*Boot and core kernel*
- BIOS and UEFI boot via [Limine](https://github.com/limine-bootloader/limine).
- Physical memory manager (bitmap), virtual memory manager with 4-level paging.
- Custom GDT and IDT, full interrupt and exception handling.
- ACPI table parsing, APIC / IOAPIC / LAPIC, HPET and APIC timer.
- SMP (multicore) initialization with per-CPU structures.
- SSE/AVX support with proper FPU state save and restore.
- Preemptive scheduler with `fork` / `exec` / `wait` / `waitpid`, pipes, and basic signals.
- ~60 kernel syscalls accessible from userspace.

*Buses and devices*
- PCI / PCIe enumeration via ACPI MCFG with legacy `0xCF8/0xCFC` fallback. Recursive bridge walk, BAR sizing (incl. 64-bit), capability-list parsing, MSI and MSI-X enable. Driver matching framework keyed on vendor/device or class/subclass.

*Filesystems and storage*
- Virtual filesystem layer with mount points.
- ext2 read/write, including in-kernel `mkfs.ext2`.
- FAT32 read/write, including in-kernel `mkfs.fat32`.
- initramfs for the Live ISO, ramfs, devfs (`/dev/tty`, `/dev/null`, `/dev/zero`).
- ATA disk driver, generic block-device layer, MBR partition table parsing.

*Userland*
- Own C library `libcervus` — POSIX-style API, ~230 source files, split one function per `.c`. Private internals isolated in a single `<libcervus.h>` header.
- 45+ utilities under `/bin`: `cat`, `ls`, `cp`, `mv`, `rm`, `mkdir`, `find`, `grep`, `head`, `tail`, `wc`, `sort`, `uniq`, `diff`, `tee`, `sleep`, `touch`, `stat`, `ps`, `kill`, `env`, `echo`, `basename`, `dirname`, `pwd`, `true`, `false`, `which`, `whoami`, `uname`, `hexdump`, `seq`, `yes`, `clear`, `reboot`, `shutdown`, `mkfs`, `mount`, `umount`, `lsblk`, `lspci`, `diskinfo`, `meminfo`, `cpuinfo`.
- Apps under `/apps`: interactive login shell, `neo` text editor (modal-free, nano-style), calculator, calendar, date, system fetch, uptime, and a set of process / IO / memory test programs.
- Two shells: the interactive login shell, and `csh` for scripting, with `if`/`else`/`endif`, `foreach`, `while`, redirects, pipes, environment variables and `$status`.
- Bundled [TCC](https://bellard.org/tcc/) — the system can compile C code on itself, without a host toolchain.

*Live ISO and installer*
- Cervus boots from ISO as a Live system using initramfs.
- The bundled `install-on-disk` wipes a chosen disk, writes an MBR, formats ESP (FAT32, 64 MB) + root (ext2) + swap (16 MB), copies the system tree, writes `limine.conf` to three locations and installs Limine BIOS stage1.
- After install the system boots directly from the disk image with no ISO required.

## Why this exists

- **No black boxes.** Every layer from `_start` to `cat` lives in this repository. ~13k lines of kernel C, plus a clearly factored libc and userland. The whole stack can be read end-to-end.
- **Not a Linux clone.** Cervus has its own kernel ABI, its own syscall numbers, its own libc. It speaks POSIX where reasonable, but is not bound to Linux internals — no `/proc` assumption, no `clone` flags, no glibc ABI.
- **Real userland.** Not a "kernel that prints hello". There is an actual shell with history, a usable text editor, a working compiler, a real filesystem on a real disk, an installer that produces a bootable system.
- **One-command build.** `./build run` produces an ISO and launches QEMU. The build system is a single C binary, checked into the repository.

## Architecture overview

```
kernel/             x86_64 kernel
  src/
    drivers/        ata, ps/2, timer, block device, partitions, pci
    fs/             vfs, ext2, fat32, initramfs, ramfs, devfs
    memory/         pmm, vmm, paging
    sched/          scheduler, task state, fork/exec
    syscall/        syscall table and dispatch
    apic/           LAPIC, IOAPIC
    acpi/           ACPI table parsing
    smp/            multicore startup
    graphics/       framebuffer + PSF font rendering
    sse/            FPU/SSE state handling

usr/
  lib/libcervus/    C library, one function per .c, private <libcervus.h>
    unistd/  fcntl/  sys/{stat,mman,wait,utsname}/
    string/  stdio/{printf,scanf}/  stdlib/  ctype/  time/
    termios/  signal/  dirent/  math/  cervus/  internal/
  apps/             interactive shell, neo editor, calc, test programs
  bin/              coreutils-style userland (45+ programs)
  installer/        disk installer (Live ISO only)
  sysroot/          public headers
  tcc/              ported Tiny C Compiler

builder/            single-binary build system written in C
limine.conf         boot config
```

## Roadmap

| Component | Status | Description |
| :--- | :---: | :--- |
| *Bootloader* | Done | Limine BIOS + UEFI |
| *Graphics / PSF font* | Done | Framebuffer, text rendering |
| *Memory (PMM/VMM)* | Done | Bitmap PMM, 4-level paging VMM |
| *Interrupts (IDT)* | Done | Exceptions and IRQs |
| *ACPI* | Partial | Table parsing, SDT discovery; reset path pending |
| *APIC / IOAPIC* | Done | Per-CPU LAPIC, IRQ routing |
| *Timers (HPET / APIC)* | Done | Periodic and one-shot |
| *SMP* | Done | Multicore boot, per-CPU state |
| *Scheduler* | Done | Preemptive multitasking, fork/exec/wait |
| *Userspace* | Done | Ring 3, ~60 syscalls, libcervus |
| *VFS + ext2/FAT32* | Done | Read/write, in-kernel mkfs |
| *Disk installer* | Done | BIOS + UEFI, MBR, ext2 root |
| *On-device C compiler* | Done | TCC ported and bundled |
| *PCI / PCIe* | Done | MCFG + legacy CF8/CFC, recursive bridge walk, MSI/MSI-X, `lspci` |
| *USB* | Done| XHCI / mass storage |
| *Networking* | Not started | TCP/IP stack |
| *GUI* | Not started | Compositor on top of framebuffer |

## Build environment

### Prerequisites

Cervus builds with a regular host Linux toolchain — no cross-compiler required.

*   *Compiler:* `gcc`
*   *Assembler:* `nasm`
*   *Archiver:* `ar` (binutils)
*   *Emulation:* `qemu-system-x86_64`
*   *ISO tools:* `xorriso`, `mtools`

### Build and run

*1. Clone the repository:*
```bash
git clone https://github.com/VeoQeo/Cervus.git
cd Cervus
```

*2. Build everything and launch in QEMU:*
```bash
./build 
```

A full list of build commands is available via `./build help`.

## Contributing

Cervus is an open-source research project. Contributions to drivers, additional utilities, bug fixes, hardware support and documentation are welcome. Open an Issue or send a Pull Request.

## License

This project is licensed under the GPL-3.0 License. See the [LICENSE](LICENSE) file for details.
