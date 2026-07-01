# Cervus OS — Technical Wiki

**Cervus** is a 64-bit, Unix-style operating system written from scratch for the
`x86_64` architecture. The kernel, C library (`libcervus`), userland, shell, text
editor, on-device C compiler and disk installer all live in a single repository.
There is no Linux compatibility layer, no BusyBox and no external libc.

> **Design goal:** a self-hosting, minimal but genuinely usable environment — boot
> from BIOS or UEFI, install onto a real disk, log in, then edit and compile C on
> the machine itself.

| | |
| :--- | :--- |
| **Architecture** | x86_64 (long mode, 4-level paging) |
| **Bootloader** | [Limine](https://github.com/limine-bootloader/limine) — BIOS + UEFI |
| **Stage** | Alpha (`v0.0.2`) |
| **License** | GPL-3.0 |
| **Language** | C (freestanding) + NASM assembly |
| **Toolchain** | Host `gcc` / `nasm` / `ar` — no cross-compiler required |

---

## Documentation map

This wiki is organised bottom-up, from boot to userland. If you are new, read
[Overview](Overview) then [Getting Started](Getting-Started).

### Core

* **[Overview](Overview)** — what Cervus is, feature matrix, source tree layout.
* **[Getting Started](Getting-Started)** — prerequisites, building, running under QEMU, installing to disk.
* **[Boot Process](Boot-Process)** — Limine handoff, `kernel_main` init order, root-device discovery.

### Kernel subsystems

* **[Memory Management](Memory-Management)** — PMM (bitmap + buddy), slab allocator, VMM, 4-level paging, HHDM, DMA.
* **[Interrupts & APIC](Interrupts-and-APIC)** — GDT, IDT, exceptions, ACPI, LAPIC/IOAPIC, HPET & APIC timers.
* **[Scheduler & SMP](Scheduler-and-SMP)** — preemptive multitasking, priority run-queues, `fork`/`exec`/`wait`, per-CPU state.
* **[System Calls](System-Calls)** — the `syscall`/`sysret` ABI and the complete numbered syscall reference.
* **[Filesystems](Filesystems)** — the VFS layer and ext2 / FAT32 / ISO9660 / ramfs / devfs / procfs / initramfs.
* **[Device Drivers](Device-Drivers)** — PCI/PCIe, disk (ATA/AHCI/NVMe), USB (xHCI/EHCI/UHCI), PS/2, timers.

### Userland & process

* **[Userland](Userland)** — `libcervus`, the shells, the `neo` editor, coreutils, the bundled TCC compiler.
* **[Security Model](Security-Model)** — UID/GID and the capability system.

### Project

* **[Contributing](Contributing)** — build conventions, commit/PR rules, memory-safety (`alex`) mode.

---

## Quick start

```bash
git clone https://github.com/VeoQeo/Cervus.git
cd Cervus
./build            # build kernel + initramfs + ISO, launch QEMU
./build help       # full command reference
```

See [Getting Started](Getting-Started) for every build mode.

---

<sub>This wiki documents the code as it exists in the repository. Where the roadmap
lists a component as *Not started* (networking, GUI), it is called out explicitly.
Follow development news on the project's [Telegram channel](https://t.me/veoqeo_off).</sub>
