# Getting Started

This page covers building Cervus, running it under QEMU, and installing it onto a
disk. The entire build is driven by a single self-contained C program,
`builder/build.c`, compiled into the `./build` binary at the repository root.

## Prerequisites

Cervus builds with a **regular host Linux toolchain** — no cross-compiler is
required.

| Tool | Purpose |
| :--- | :--- |
| `gcc` | Compiles the kernel, libc and userland (freestanding) |
| `nasm` | Assembles the `.asm` sources |
| `ar` (binutils) | Archives `libcervus.a` |
| `qemu-system-x86_64` | Emulation / run target |
| `xorriso`, `mtools` | ISO / FAT image construction |

At first build the builder fetches three pinned dependencies into the tree
(commits are hard-coded in `builder/build.c`):

| Dependency | Source |
| :--- | :--- |
| `freestnd-c-hdrs` | Freestanding C headers (OSDev) |
| `cc-runtime` | Compiler runtime helpers (OSDev) |
| `limine-protocol` | Limine boot-protocol headers |

## Building and running

```bash
# Build the ./build binary first (if not already present), then:
./build                 # Interactive TUI menu
./build run             # Build kernel + initramfs + ISO, launch QEMU (BIOS + IDE)
./build help            # Full command reference
```

### `run` options

Options can be combined freely.

| Option | Effect | Default |
| :--- | :--- | :--- |
| `--uefi` | Boot via UEFI / OVMF | BIOS |
| `--disk=MODE` | `ide` \| `ahci` \| `nvme` \| `all` \| `none` | `ide` |
| `--live` | No disk, boot the ISO live (same as `--disk=none`) | — |
| `--fresh` | Recreate empty disk image(s) before boot | — |
| `--installed` | Boot the existing disk only, no ISO (simulate real hardware) | — |
| `--no-clean` | Keep `obj/` and `bin/` after the run | — |
| `--no-initramfs` | Skip `initramfs.tar` creation | — |

**Examples**

```bash
./build run                    # BIOS + IDE disk
./build run --disk=ahci        # BIOS + AHCI/SATA disk
./build run --uefi --disk=all  # UEFI, all disk controller types
./build run --live             # Live, no disk
./build run --installed        # Boot the already-installed disk only
```

The default QEMU invocation uses `-m 8G -smp 8 -cpu qemu64,+fsgsbase` with a GTK
display. The `+fsgsbase` CPU flag is required — Cervus uses `FSGSBASE`
instructions for per-CPU data.

### Other commands

| Command | Purpose |
| :--- | :--- |
| `./build flash` | Flash the latest ISO to a USB device (needs `sudo`) |
| `./build hardware` | Install Cervus into the host's Limine boot menu (needs `sudo`) |
| `./build clean` | Remove all build artifacts |
| `./build tree [files]` | Generate `OS-TREE.txt` |
| `./build alex` | Rebuild everything with AddressSanitizer (memory-safety mode) |

Global flags: `--tree` / `--structure-only` (generate the source tree),
`--resethardwareconf` (restore the host's original Limine config).

## The Live ISO

`./build run` (without `--installed`) boots a **Live system**: Limine loads the
kernel plus two modules — the `init` ELF and an `initramfs.tar` — and the kernel
mounts the initramfs as root. Nothing is written to disk. This is the fastest way
to iterate.

## Installing to a disk

From inside a running Live system, the bundled installer writes Cervus to a real
(or virtual) disk:

1. Wipes the chosen disk and writes an MBR.
2. Creates three partitions: **ESP** (FAT32, 64 MB), **root** (ext2) and
   **swap** (16 MB).
3. Copies the system tree onto the root partition.
4. Writes `limine.conf` to three locations and installs the Limine BIOS stage-1.

After installation the machine boots directly from the disk with no ISO. To
emulate this locally:

```bash
./build run --fresh --disk=ide      # create a blank disk, run the Live installer
# ...run the installer inside Cervus, then:
./build run --installed --disk=ide  # boot the installed disk, no ISO
```

## Root-device discovery

On boot the kernel decides what to use as root (see
[Boot Process](Boot-Process) for the full sequence):

* If an **initramfs module** is present (ISO boot), it is mounted as root.
* Otherwise the kernel probes for an **installed system** by scanning known disk
  prefixes (`nvme0n1`, `sda`, `hda`) for an ext2 root partition, and boots from
  it.
* If neither is found, it falls back to an in-memory `ramfs`.

---

Next: **[Boot Process](Boot-Process)** for what happens between power-on and the
first userland process.
