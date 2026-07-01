# Boot Process

This page walks through everything between power-on and the first userland
process (`init`). The authoritative source is
[`kernel/src/kernel.c`](../blob/main/kernel/src/kernel.c).

## 1. Firmware → Limine

Cervus does not ship its own bootloader; it uses **Limine** (BIOS and UEFI). The
kernel is an ELF that declares a set of *Limine requests* — structures placed in
dedicated `.limine_requests*` sections that Limine fills in before jumping to the
kernel entry point. Cervus uses **Limine base revision 4**.

Requested features:

| Request | Provides |
| :--- | :--- |
| `framebuffer` | Linear framebuffer (address, pitch, width/height, bpp) |
| `memmap` | Firmware memory map |
| `hhdm` | Higher-Half Direct Map offset |
| `mp` | Multiprocessor (SMP) info — AP list |
| `rsdp` | ACPI RSDP pointer |
| `module` | Boot modules: the `init` ELF and (on ISO) `initramfs.tar` |

Limine enters long mode, sets up an initial page table with the HHDM, and calls
`kernel_main()`.

## 2. `kernel_main` — initialisation order

The boot sequence is deliberately ordered; each stage depends on the ones before
it. Progress is logged to the COM1 serial port (`115200 8N1`) and, once the
framebuffer console is up, to the screen.

```
serial_initialize(COM1, 115200)      # earliest possible diagnostics
check Limine base revision supported

gdt_init()                           # GDT + TSS
init_interrupt_system() / IDT        # exception + IRQ vectors
fpu_init(); sse_init()               # enable FPU/SSE
enable_fsgsbase()                    # FSGSBASE for per-CPU data

# validate framebuffer / memmap / hhdm responses (halt if missing)

pmm_init(memmap, hhdm)               # physical memory manager
slab_init()                          # slab allocator on top of PMM
paging_init()                        # kernel page tables
vmm_init()                           # virtual memory manager
fb_init_backbuffer(); vt_init()      # framebuffer + virtual terminal

vfs_init()                           # virtual filesystem core
  mount ramfs at "/"
  mkdir /dev /bin /etc /tmp /proc /mnt
  mount devfs at "/dev"
  mount procfs at "/proc"

acpi_init(); acpi_print_tables()     # ACPI table discovery
apic_init()                          # LAPIC + IOAPIC
pci_init()                           # PCI/PCIe enumeration
smp_init(mp_response)                # bring up application processors
  wait (<=5 s) for all APs online

syscall_init()                       # program SYSCALL/SYSRET MSRs
disk_init()                          # probe ATA / AHCI / NVMe
xhci_init(); ehci_init(); uhci_init()# USB host controllers

# --- root device selection (see below) ---

timer_init()                         # HPET / APIC timer
ps2_init()                           # PS/2 keyboard + mouse
sched_init(); sched_notify_ready()   # scheduler online
load_elf_module()                    # load and start init (ELF)
sched_reschedule()                   # hand control to the scheduler
```

After `sched_reschedule()` the boot CPU never returns to `kernel_main`; it either
runs tasks or halts (`hlt`) in the idle loop while interrupts drive scheduling.

## 3. Root-device selection

Before starting `init`, the kernel decides what filesystem is root:

1. **ISO / Live boot** — if Limine passed **two or more modules** (the `init` ELF
   *and* an `initramfs.tar`), the kernel treats this as an ISO boot and does
   **not** auto-mount a disk. The initramfs is mounted as root.

2. **Installed system** — otherwise `find_installed_root()` scans the disk name
   prefixes `nvme0n1`, `sda`, `hda` and looks for:
   * an **ext2 signature** on partition 2 (`…p2` / `…2`), the normal install
     layout; or
   * a **FAT32 ESP** on partition 1 with an ext2 sibling on partition 2; or
   * a legacy **ext2-on-whole-disk** image.

   The ext2 magic is checked by reading the superblock at
   `EXT2_SUPER_OFFSET + 56`. If a root is found, `/` is unmounted (from the boot
   ramfs) and the disk is mounted in its place, then `/dev /tmp /proc /mnt` are
   recreated.

3. **Fallback** — if a disk was expected but the mount failed, the kernel falls
   back to a fresh in-memory `ramfs` so the system still reaches a shell.

## 4. Starting `init`

`load_elf_module()` takes the first Limine module (the `init` ELF) and:

1. Calls `elf_load()` to map its `PT_LOAD` segments into a fresh user pagemap.
   The loader supports `ET_EXEC` and position-independent `ET_DYN` (PIE, base
   `0x400000`). See [System Calls → ELF & process image](System-Calls#process-image).
2. Builds the initial user stack via `elf_build_init_stack()` (top at
   `0x00007FFFFFFFE000`, 16-byte aligned).
3. Creates a ring-3 task with `task_create_user("init", …, priority 16)` and sets
   its heap break (`brk_start` / `brk_current`) to the end of the loaded image.
4. Assigns standard I/O with `vfs_init_stdio()` (fds 0/1/2 → `/dev/tty`).
5. Starts the background worker tasks: USB (xHCI/EHCI/UHCI) hotplug workers, the
   disk media worker, and the timer recalibration task.
6. Calls `console_boot_logging_off()` — boot chatter stops and the console
   belongs to userland.

From here, `init` runs in ring 3, spawns the login shell, and the system is
interactive. See [Userland](Userland) for what happens next.

## Diagnostics

If the machine hangs during boot, the **serial log** is the first place to look —
every stage prints an `[OK]` or `[stage] …` marker to COM1. Under QEMU, add a
serial redirect or read the emitted `log.txt`. A missing framebuffer, memory map
or HHDM response causes an immediate halt (`hcf`) with an explanatory serial
message.

---

Next: **[Memory Management](Memory-Management)**.
