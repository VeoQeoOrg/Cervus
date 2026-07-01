# Memory Management

Cervus manages memory in three layers: a **physical memory manager (PMM)** that
owns page frames, a **slab allocator** for small kernel objects, and a **virtual
memory manager (VMM)** that manipulates 4-level page tables. All three sit above
the Limine memory map and the Higher-Half Direct Map (HHDM).

Relevant sources: `kernel/src/memory/{pmm,vmm,paging,dma}.c` and the mirrored
headers under `kernel/include/memory/`.

## Address space layout

x86_64 long mode uses 48-bit canonical virtual addresses, split into two halves:

| Range | Use |
| :--- | :--- |
| `0x0000_0000_0000_0000` – `0x0000_7FFF_FFFF_FFFF` | **User space** (128 TiB) |
| `0xFFFF_8000_0000_0000` – `0xFFFF_FFFF_FFFF_FFFF` | **Kernel / higher half** |

* **HHDM** — Limine maps all physical RAM at a fixed higher-half offset
  (`hhdm.offset`). The kernel converts between physical and virtual with
  `pmm_phys_to_virt()` / `pmm_virt_to_phys()` instead of touching identity
  mappings.
* The user/kernel boundary for `mmap` and stack placement is
  `0x0000_8000_0000_0000`; requests crossing it are rejected.
* The **user stack top** is `0x0000_7FFF_FFFF_E000`; PIE executables load at base
  `0x0000_0000_0040_0000`.

`PAGE_SIZE` is `4096` (`PAGE_SHIFT = 12`).

## Physical Memory Manager (PMM)

`pmm_init()` consumes the Limine memory map and builds a page-frame allocator.
Physical memory below `0x100000` (1 MiB) is never handed out.

The PMM is a **buddy allocator**:

* Free frames are grouped into power-of-two blocks (*orders*). `PMM_MAX_ORDER`
  is `10`, i.e. the largest block is `2^10 × 4 KiB = 4 MiB`.
* Allocation of order *n* splits a larger free block if necessary; freeing merges
  buddies back together to fight fragmentation.
* A bitmap tracks frame state for the boot handoff and statistics
  (`pmm_print_stats()`).

Key API:

```c
void*   pmm_alloc_page(void);          // one 4 KiB frame
void*   pmm_alloc_pages(size_t order); // 2^order contiguous frames
void    pmm_free_page(void* p);
void    pmm_free_pages(void* p, size_t order);
void*   pmm_phys_to_virt(uintptr_t phys);
uintptr_t pmm_virt_to_phys(void* virt);
```

## Slab allocator

Buddy blocks are coarse (page-granular), so kernel objects smaller than a page go
through a **slab allocator** initialised by `slab_init()`.

* `SLAB_NUM_CACHES = 10` size classes span `SLAB_MIN_SIZE = 8` bytes to
  `SLAB_MAX_SIZE = 4096` bytes.
* Each `slab_cache_t` keeps `partial` and `full` slab lists; a slab is a page (or
  block) carved into fixed-size objects.
* This backs the kernel's general-purpose `kmalloc`/`kfree`-style allocations
  (task structs, vnodes, fd tables, driver state, …).

Statistics are available via `slab_print_stats()`.

## Virtual Memory Manager (VMM) & paging

`paging_init()` builds the kernel's own page tables (rather than continuing to use
Limine's), and `vmm_init()` brings up the VMM that userland and drivers use.

Cervus uses standard x86_64 **4-level paging**: PML4 → PDPT → PD → PT. Each task
owns a `vmm_pagemap_t` whose `pml4` root becomes `CR3` when the task is scheduled.

Page-table entry flags (`kernel/include/memory/vmm.h`):

| Flag | Bit | Meaning |
| :--- | :---: | :--- |
| `VMM_PRESENT` | 0 | Entry is valid |
| `VMM_WRITE` | 1 | Writable |
| `VMM_USER` | 2 | Accessible from ring 3 |
| `VMM_PWT` | 3 | Page write-through |
| `VMM_PCD` | 4 | Page cache disable |
| `VMM_ACCESSED` | 5 | Set by CPU on access |
| `VMM_DIRTY` | 6 | Set by CPU on write |
| `VMM_PSE` / `VMM_PAT` | 7 | Large page / PAT |
| `VMM_GLOBAL` | 8 | Global TLB entry |
| `VMM_NOEXEC` | 63 | No-execute (requires EFER.NXE) |

The VMM handles mapping/unmapping ranges, creating and destroying per-task
pagemaps (used by `fork` and `execve`), and copy semantics on process creation.
On SMP systems, TLB shootdown is required when unmapping higher-half
(`>= 0xffff800000000000`) pages that other CPUs may have cached.

## User memory: `brk` and `mmap`

Userland grows its heap and maps memory through syscalls that drive the VMM:

* **`brk` / `sbrk`** — each task tracks `brk_start`, `brk_current` and `brk_max`.
  `sys_brk` moves the program break, backing new pages on demand.
* **`mmap` / `munmap`** — anonymous, private mappings
  (`MAP_PRIVATE | MAP_ANONYMOUS`) with `PROT_READ/WRITE/EXEC`. `MAP_FIXED` is
  supported. The mapping region is bounded by the user/kernel split at
  `0x0000_8000_0000_0000`; requests past it return `MAP_FAILED`.

See [System Calls](System-Calls) for the exact signatures and flag values.

## DMA memory

`kernel/src/memory/dma.c` provides physically-contiguous, identity-tracked buffers
for device drivers (USB controllers, AHCI, NVMe) that program the hardware with
physical addresses. DMA allocations come from the PMM but are exposed with both
their physical and HHDM-virtual addresses so drivers can hand the physical address
to the device while the kernel accesses the same memory virtually.

---

Next: **[Interrupts & APIC](Interrupts-and-APIC)**.
