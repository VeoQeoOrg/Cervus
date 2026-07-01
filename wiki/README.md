# Cervus Wiki sources

These Markdown files are the **technical wiki** for Cervus OS, written to be
published directly as the project's GitHub Wiki. They also read fine as plain docs
in the repository.

## Pages

| File | Page |
| :--- | :--- |
| `Home.md` | Landing page / documentation map |
| `Overview.md` | What Cervus is, feature matrix, source tree |
| `Getting-Started.md` | Build, run, install |
| `Boot-Process.md` | Firmware → Limine → `kernel_main` → `init` |
| `Memory-Management.md` | PMM, slab, VMM, paging, DMA |
| `Interrupts-and-APIC.md` | GDT/IDT, ACPI, LAPIC/IOAPIC, timers |
| `Scheduler-and-SMP.md` | Tasks, scheduling, context switch, SMP, processes |
| `System-Calls.md` | ABI + complete numbered syscall reference |
| `Filesystems.md` | VFS + ext2/FAT32/iso9660/ram/dev/proc/initramfs |
| `Device-Drivers.md` | PCI, storage, USB, input, console |
| `Userland.md` | libcervus, init, shells, apps, coreutils, TCC |
| `Security-Model.md` | UID/GID + capabilities |
| `Contributing.md` | Workflow, commit/PR rules, ASan mode |
| `_Sidebar.md`, `_Footer.md` | GitHub Wiki navigation chrome |

## Publishing to the GitHub Wiki

The GitHub Wiki is a separate git repository (`<repo>.wiki.git`). To publish:

```bash
# 1. Enable the Wiki in the repo settings and create the first page in the UI
#    (this initialises the wiki repo).

# 2. Clone the wiki repo somewhere outside this tree:
git clone https://github.com/VeoQeo/Cervus.wiki.git

# 3. Copy these files in and push:
cp /path/to/Cervus/wiki/*.md Cervus.wiki/
cd Cervus.wiki
git add .
git commit -s -m "docs: import technical wiki"
git push
```

GitHub maps a page file name to its title by replacing hyphens with spaces, and
resolves `[Text](Page-Name)` links to sibling pages. `_Sidebar.md` renders on the
right of every page; `_Footer.md` renders at the bottom.

## Conventions

* Internal links use `[Text](Page-Name)` (the file name without `.md`).
* Links into the repository use `../blob/main/<path>` so they resolve from the
  wiki to the code.
* Content documents the code **as it exists**; roadmap-only items (networking,
  GUI) are marked as not implemented.
