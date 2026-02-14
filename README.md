<p align="center">
  <img src="https://github.com/VeoQeo/Cervus/blob/main/wallpapers/cervus_logo.jpg" alt="Cervus OS Logo" width="400px">
</p>


# Cervus x86_64 Operating System

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey.svg)](https://en.wikipedia.org/wiki/X86-64)
[![Stage: Alpha](https://img.shields.io/badge/Stage-Alpha-orange.svg)]()

**Cervus** - This is a modern 64-bit operating system written for the x86_64 architecture. It is currently under development.

---

## Technical Features

Cervus is currently in the active development phase. The kernel implements core low-level primitives required for a stable execution environment:

### Core Architecture
- *Boot Protocol:* Utilizes the [Limine](https://github.com/limine-bootloader/limine) bootloader (Barebone profile).
- *Memory Management:*
    - *PMM:* Bitmap-based Physical Memory Manager.
    - *VMM:* Virtual Memory Management with 4-level paging support.
- *CPU Initialization:* Custom GDT (Global Descriptor Table) and IDT (Interrupt Descriptor Table) implementation.
- *Acceleration:* Native support for *SIMD* instructions (SSE/AVX) with proper state saving.

## Roadmap & Progress

| Component | Status | Description |
| :--- | :---: | :--- |
| *Bootloader* | Done | Limine Integration |
| *Graphics/PSF* | Done | Framebuffer & Text Rendering |
| *Memory (PMM/VMM)* | Done | Physical & Virtual Memory Management |
| *Interrupts (IDT)* | Done | Handling exceptions and IRQs |
| *ACPI* | Done(without rebooting) | Table parsing & SDT discovery |
| *APIC / IOAPIC* | Done | Advanced Interrupt Controllers |
| *Timers (HPET/APIC)* | Done | High Precision Event Timers |
| *SMP* | Done | Multicore Initialization |
| *Scheduler* | Done | Preemptive Multitasking |
| *Userspace* | TODO | Syscalls & Ring 3 execution |

---

## Build Environment

### Prerequisites

To build Cervus, you need a cross-compilation toolchain and the following utilities:

*   *Compiler:* `x86_64-elf-gcc`
*   *Assemblers:* `nasm`, `gas`
*   *Emulation:* `qemu-system-x86_64`
*   *ISO Tools:* `xorriso`, `mtools`

### Compiling and Running

*1. Clone the repository:*

```bash
bash
git clone https://github.com/VeoQeo/Cervus.git
cd Cervus
```

*2. Compile and launch in QEMU:*
```bash
./build run
```

*3. Deploy to hardware (Flash Drive):*
**WARNING: This will overwrite data on the target device.**
```bash
sudo ./build flash
```

## Contributing

Cervus is an open-source research project. Contributions regarding bug fixes, hardware support, or documentation are welcome. Please feel free to open an Issue or submit a Pull Request.

## License

This project is licensed under the *GPL-3.0 License*. See the [LICENSE](LICENSE) file for details.

---
