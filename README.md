<p align="center">
  <img src="https://github.com/VeoQeo/Cervus/blob/main/wallpapers/cervus_logo.jpg" alt="Cervus OS Logo" width="400px">
</p>


# 🦌 Cervus x86_64 Operating System

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey.svg)](https://en.wikipedia.org/wiki/X86-64)
[![Stage: Alpha](https://img.shields.io/badge/Stage-Alpha-orange.svg)]()

Cervus is a modern, hobbyist 64-bit operating system built from the ground up for the *x86_64* architecture. It focuses on modularity, modern hardware support, and leveraging higher-level architectural features like SIMD and advanced memory management.

---

## Technical Features

Cervus is currently in the active development phase. The kernel implements core low-level primitives required for a stable execution environment:

### Core Architecture
- *Boot Protocol:* Utilizes the [Limine](https://limine-bootloader.org/) bootloader (Barebone profile).
- *Memory Management:* 
    - *PMM:* Bitmap-based Physical Memory Manager.
    - *VMM:* Virtual Memory Management with 4-level paging support.
- *CPU Initialization:* Custom GDT (Global Descriptor Table) and IDT (Interrupt Descriptor Table) implementation.
- *Acceleration:* Native support for *SIMD* instructions (SSE/AVX) with proper state saving.

### Hardware Interfacing
- *Graphics:* Linear Framebuffer (LFB) support via Limine with PSF font rendering.
- *Diagnostics:* Kernel logging through Serial COM1 port and basic I/O abstractions.
- *ACPI:* Advanced Configuration and Power Interface table parsing for hardware discovery.

---

## 🛠 Roadmap & Progress

| Component | Status | Description |
| :--- | :---: | :--- |
| *Bootloader* | ✅ | Limine Integration |
| *Graphics/PSF* | ✅ | Framebuffer & Text Rendering |
| *Memory (PMM/VMM)* | ✅ | Physical & Virtual Memory Management |
| *Interrupts (IDT)* | ✅ | Handling exceptions and IRQs |
| *ACPI* | ✅ | Table parsing & SDT discovery |
| *APIC / IOAPIC* | 🏗️ | Advanced Interrupt Controllers |
| *Timers (HPET/APIC)* | 📅 | High Precision Event Timers |
| *SMP* | 📅 | Multicore Initialization |
| *Scheduler* | 📅 | Preemptive Multitasking |
| *Userspace* | 📅 | Syscalls & Ring 3 execution |

---

## 🏗 Build Environment

### Prerequisites

To build Cervus, you need a cross-compilation toolchain and the following utilities:

*   *Compiler:* `x86_64-elf-gcc` (или подходящий кросс-компилятор)
*   *Assemblers:* `nasm`, `gas`
*   *Emulation:* `qemu-system-x86_64`
*   *ISO Tools:* `xorriso`, `mtools`

### Compiling and Running

*1. Clone the repository:*

```bash
bash
git clone https://github.com/yourusername/cervus.git
cd cervus
```


*2. Compile and launch in QEMU:*
```bash
./build run
```


*3. Deploy to hardware (Flash Drive):*
*⚠️ Warning: This will overwrite data on the target device.*
```bash
sudo ./build flash
```


---

## 📁 Project Structure

```text
.
├── src/
│   ├── kernel/     # Core kernel logic (C/ASM)
│   ├── drivers/    # Hardware abstraction layers
│   └── include/    # Kernel headers and libc definitions
├── build/          # Build artifacts and ISO image
├── limine/         # Bootloader files
└── scripts/        # Build and deployment automation
```


---

## 🤝 Contributing

Cervus is an open-source research project. Contributions regarding bug fixes, hardware support, or documentation are welcome. Please feel free to open an Issue or submit a Pull Request.

## 📄 License

This project is licensed under the *GPL-3.0 License*. See the [LICENSE](LICENSE) file for details.

---
