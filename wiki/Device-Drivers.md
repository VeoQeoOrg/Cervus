# Device Drivers

This page covers the hardware drivers: PCI/PCIe enumeration, the storage stack
(ATA / AHCI / NVMe + partitions), USB (three host-controller families), and the
input/console devices (PS/2, mouse, keymap, serial, framebuffer).

Sources: `kernel/src/drivers/`, `kernel/src/graphics/fb/`, `kernel/src/io/`,
`kernel/src/console/`.

## PCI / PCIe

`pci_init()` (`drivers/pci.c`) enumerates the PCI configuration space:

* **Access method** — prefers ECAM (memory-mapped config) using the base(s) from
  the ACPI **MCFG** table; falls back to the legacy `0xCF8`/`0xCFC` I/O port
  mechanism when MCFG is absent.
* **Recursive bridge walk** — follows PCI-to-PCI bridges to enumerate every bus.
* **BAR sizing** — determines each Base Address Register's size and type,
  including 64-bit BARs, memory vs I/O, and prefetchable regions.
* **Capabilities** — parses the capability list and enables **MSI** and **MSI-X**
  where a driver requests interrupts.
* **Driver matching** — a small framework matches drivers to devices by
  vendor/device ID or by class/subclass.

The enumerated device list is exposed to userland via `SYS_PCI_LIST` (struct
`cervus_pci_device_t`, including its `cervus_pci_bar_t bars[6]`), which backs the
`lspci` utility.

## Storage

The block stack is layered so filesystems never talk to a controller directly:

```
filesystem  →  partition  →  blkdev  →  { ATA | AHCI | NVMe | USB-MSC }
```

### Generic block device (`blkdev`)

`drivers/disk/blkdev.c` provides a uniform `blkdev_t` with `read_sectors` /
`write_sectors` operations and a name (`sda`, `nvme0n1`, …). Higher layers look
devices up by name (`blkdev_get_by_name`) and read/write byte ranges
(`blkdev_read`). `disk_init()` probes all controller types at boot; a background
**media worker** (`disk_start_media_worker`) handles removable-media changes.

### Controllers

| Driver | Bus | Notes |
| :--- | :--- | :--- |
| **ATA** (`ata.c`) | Legacy IDE | PIO read/write; the default QEMU `--disk=ide` target |
| **AHCI** (`ahci.c`) | SATA | Native command list / FIS; `--disk=ahci` |
| **NVMe** (`nvme.c`) | PCIe | Admin + I/O queues; `--disk=nvme` |

### Partitions

`drivers/disk/partition.c` parses partition tables and registers each partition as
its own `blkdev` (e.g. `sda1`, `nvme0n1p2` — note the `p` separator when the base
name ends in a digit):

* **MBR** — classic 4-entry table; boot flag, type, LBA start, sector count.
* **GPT** — GUID partition table (written by `SYS_DISK_PARTITION_GPT`).

Userland enumerates partitions via `SYS_DISK_LIST_PARTS` (`cervus_part_info_t`),
backing `lsblk` / `fdisk` / `diskinfo`.

## USB

Cervus supports all three PC USB host-controller generations, each under
`drivers/usb/<hc>/`, sharing a common enumeration/configuration core:

| Controller | USB gen | Directory |
| :--- | :--- | :--- |
| **UHCI** | USB 1.x (Intel) | `usb/uhci/` |
| **EHCI** | USB 2.0 | `usb/ehci/` |
| **xHCI** | USB 3.x | `usb/xhci/` |

Shared logic (`usb/usb_enum.c`, `usb/usb_config.c`, `usb/usb_hid.c`,
`usb/usb_msc.c`):

* **Enumeration** — reset, address assignment, descriptor reads, configuration
  selection.
* **Class drivers**, present per controller as `hid.c`, `hub.c`, `msc.c`:
  * **HID** — USB keyboards and mice.
  * **Hub** — downstream-port enumeration for hubs.
  * **MSC (Mass Storage Class)** — Bulk-Only Transport; USB storage appears as a
    `blkdev` and plugs into the [storage stack](#storage).

Each controller family runs a background **hotplug worker** started at the end of
boot (`xhci_start_worker()`, `ehci_start_worker()`, `uhci_start_worker()`).
`SYS_USB_LIST` enumerates attached devices for `lsusb`, and controller counts are
printed at boot.

## Input & console

### PS/2

`ps2_init()` (`drivers/ps2.c`) drives the 8042 controller for a PS/2 keyboard and
mouse. Keyboard scancodes are translated through the **keymap**
(`drivers/keymap.c`), which is configurable from userland via
`SYS_KEYMAP_CONFIG`. Mouse packets feed `drivers/mouse.c`, whose state is read by
`SYS_MOUSE_STATE` (`cervus_mouse_info_t`: position, buttons, scroll).

### Serial

`io/serial.c` drives the 16550 UART on **COM1 at 115200 8N1**. It is the earliest
diagnostic channel — every boot stage logs here — and remains available for kernel
`serial_printf` debugging throughout. `io/ports.h` provides the `inb`/`outb`
port-I/O primitives used across drivers.

### Framebuffer & console

* **Framebuffer** (`graphics/fb/fb.c`) — a linear framebuffer from Limine, with a
  back-buffer (`fb_init_backbuffer`) for flicker-free drawing. Text is rendered in
  software from a **PSF font** (`kernel/src/font.psf`).
* **Virtual terminal** (`console/vt.c`) — a text console on top of the
  framebuffer: scrolling, colour, and a controlling-TTY abstraction. `console/`
  also holds the kernel log ring (`klog.c`, exposed to userland via `SYS_KLOG` /
  `dmesg`) and a monitor.
* Userland can take direct control of the display via the `SYS_FB_*` syscalls
  (info / map / blit / acquire / release) — the foundation for a future GUI.

## Timers

Covered in detail under [Interrupts & APIC](Interrupts-and-APIC#timers): the HPET
provides a monotonic counter and one-shot sleeps, while the per-CPU APIC timer
drives scheduler preemption.

---

Next: **[Userland](Userland)**.
