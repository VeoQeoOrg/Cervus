# Interrupts & APIC

This page covers the descriptor tables (GDT/IDT), CPU exception and hardware
interrupt handling, ACPI table discovery, and the APIC interrupt-controller stack
(LAPIC + IOAPIC) together with the system timers.

Sources: `kernel/src/gdt/`, `kernel/src/interrupts/`, `kernel/src/acpi/`,
`kernel/src/apic/`, `kernel/src/drivers/timer.c`.

## GDT and TSS

`gdt_init()` installs a flat 64-bit **Global Descriptor Table** with the segments
required for long mode: kernel code/data (ring 0), user code/data (ring 3), and a
**Task State Segment (TSS)**. The TSS supplies the kernel stack pointer used on
privilege transitions (ring 3 → ring 0) via interrupts and `syscall`. The GDT
load itself is in `gdt/gdt_asm.asm`.

## IDT, exceptions and IRQs

`init_interrupt_system()` builds the 256-entry **Interrupt Descriptor Table**.
Vectors are split into three roles:

| Vectors | Role | Handler |
| :--- | :--- | :--- |
| `0–31` | CPU **exceptions** (faults/traps) | `interrupts/isr/isr.c` |
| `32+` | Hardware **IRQs** | `interrupts/irq/irq.c` |
| — | Software / IPI vectors | APIC layer |

The low-level entry stubs live in `interrupts/interrupt_trap.asm`: each vector
pushes its number and a (possibly dummy) error code, saves the register frame, and
calls into the C dispatcher. Exceptions such as **#PF (page fault, 14)**,
**#GP (13)**, **#DF (double fault, 8)** and **#UD (6)** are decoded; an
unrecoverable fault routes to the [panic handler](../blob/main/kernel/src/panic/panic.c),
which prints the faulting state (registers, CR2, error code) to serial and screen.

IRQ handlers can be registered by number so drivers (timer, PS/2, disk, USB) attach
to their vectors after the IOAPIC routes the line.

## FPU / SSE and FSGSBASE

Right after the IDT, the boot CPU enables floating point and vector state:

* `fpu_init()` / `sse_init()` — enable the FPU and SSE, clearing `CR0.EM`,
  setting `CR0.MP`, and enabling `CR4.OSFXSR` / `CR4.OSXMMEXCPT`.
* `enable_fsgsbase()` — enables `CR4.FSGSBASE` so the kernel can read/write the
  `GS` base cheaply for per-CPU data (see [Scheduler & SMP](Scheduler-and-SMP)).

Per-task FPU state (512-byte, 16-byte-aligned `FXSAVE` area) is saved and restored
on context switch only when a task has actually used the FPU — see `fpu_save()` /
`fpu_restore()`.

## ACPI

`acpi_init()` locates the **RSDP** (passed by Limine), then walks the RSDT/XSDT to
discover system description tables. `acpi_print_tables()` dumps what it finds. The
tables consumed here drive later stages:

* **MADT (APIC)** — LAPIC/IOAPIC addresses, per-CPU LAPIC IDs, interrupt source
  overrides.
* **MCFG** — PCI Express ECAM base(s), used by [PCI enumeration](Device-Drivers#pci--pcie).
* **HPET** — high-precision timer block.

> **Status:** ACPI is *partial* — table parsing and SDT discovery work; a full
> ACPI reset/power path is still pending (see the roadmap in [Overview](Overview)).

## APIC: LAPIC and IOAPIC

`apic_init()` replaces the legacy 8259 PIC with the **APIC** architecture:

* **LAPIC** (`apic/lapic.c`) — one per CPU. Handles the local timer, IPIs
  (inter-processor interrupts, used to start APs and for cross-CPU signalling),
  and EOI. Programmed via MMIO (or x2APIC MSRs where available).
* **IOAPIC** (`apic/ioapic.c`) — routes external device IRQ lines to LAPIC
  vectors on a chosen CPU, honouring the interrupt source overrides from the
  MADT.
* `apic/apic.c` ties the two together and provides the generic
  enable/route/EOI interface used by drivers.

## Timers

`timer_init()` (`drivers/timer.c`) sets up the system time base. Cervus supports
two hardware timers:

* **HPET** — used as a monotonic, high-resolution counter and for one-shot
  sleeps.
* **APIC timer** — the per-CPU LAPIC timer, programmed periodically to drive
  **preemption** (the scheduler tick) and one-shot for precise deadlines.

Utilities exposed to the rest of the kernel:

```c
void timer_sleep_ms(uint64_t ms);      // busy/blocked sleep
void timer_start_recal_task(void);     // background recalibration task
```

A background **recalibration task** (started during `init` handoff) periodically
re-measures the timer frequency so long-running sleeps and the monotonic clock
stay accurate. The `clocksource` abstraction (`kernel/src/time/clocksource.c`)
generalises the counter used for `CLOCK_MONOTONIC` and uptime.

## Interaction with scheduling

The APIC timer interrupt is the heartbeat of preemption: on each tick the current
task's time slice is decremented, and when it reaches zero a reschedule is
requested (`need_resched` on the per-CPU area). The actual context switch happens
at a safe point — on IRQ return or after a syscall — so the scheduler never
switches with locks held mid-critical-section. See
[Scheduler & SMP](Scheduler-and-SMP).

---

Next: **[Scheduler & SMP](Scheduler-and-SMP)**.
