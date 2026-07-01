# System Calls

Cervus has its own kernel ABI — its own syscall numbers, its own semantics — not
Linux's. Userland reaches the kernel through the `syscall`/`sysret` fast-path.
This page documents the calling convention, the dispatch mechanism, and the
complete numbered syscall table.

Sources: `kernel/src/syscall/` (one file per syscall),
`kernel/include/syscall/syscall_nums.h` (numbers and shared structs),
`kernel/src/syscall/syscall_asm.asm` (entry stub).

## Calling convention

Cervus uses the AMD64 `SYSCALL` instruction. The register usage mirrors the
System V AMD64 syscall ABI:

| Register | Role |
| :--- | :--- |
| `rax` | Syscall number (in) / return value (out) |
| `rdi` | Argument 1 |
| `rsi` | Argument 2 |
| `rdx` | Argument 3 |
| `r10` | Argument 4 |
| `r8` | Argument 5 |
| `r9` | Argument 6 |
| `rcx`, `r11` | Clobbered by `syscall` (saved user RIP / RFLAGS) |

Return convention: a non-negative value on success; a **negative errno** on
failure (see `kernel/include/syscall/errno.h`). `libcervus` wrappers translate the
negative return into the global `errno` and a `-1` result.

### Entry path

`syscall_init()` programs the MSRs that make `SYSCALL`/`SYSRET` work:

* `EFER.SCE` — enable the syscall extension (also sets `EFER.NXE` for NX).
* `STAR` — kernel/user code+stack segment selectors.
* `LSTAR` — the entry point, `syscall_entry` (in `syscall_asm.asm`).
* `SFMASK` — RFLAGS bits cleared on entry (e.g. IF, so entry is atomic).

`syscall_entry` does `swapgs`, switches to the per-CPU **kernel stack**, saves the
user register frame into the per-CPU area, shuffles the argument registers into C
calling convention and calls:

```c
int64_t syscall_handler_c(uint64_t nr, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);
```

The handler bounds-checks `nr` against `SYSCALL_TABLE_SIZE` (`568`) and dispatches
through a static function table:

```c
static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = { ... };
// nr out of range or unimplemented slot -> -ENOSYS
```

On return, the stub checks the per-CPU `need_resched` flag and may call
`sched_reschedule()` before `sysret` back to ring 3 — this is where a
timer-driven preemption is actually taken.

## Syscall number map

Numbers are grouped into ranges. The POSIX-style calls occupy the low numbers;
Cervus-specific calls start at `SYS_CERVUS_BASE = 512`.

### Process & scheduling (0–18)

| # | Name | Signature (args) |
| :--- | :--- | :--- |
| 0 | `SYS_EXIT` | `(code)` |
| 1 | `SYS_EXIT_GROUP` | `(code)` |
| 2 | `SYS_GETPID` | `()` |
| 3 | `SYS_GETPPID` | `()` |
| 4 | `SYS_FORK` | `()` → child pid / 0 |
| 5 | `SYS_WAIT` | `(pid, status*, flags)` — `WNOHANG` |
| 6 | `SYS_YIELD` | `()` |
| 7 | `SYS_GETUID` | `()` |
| 8 | `SYS_GETGID` | `()` |
| 9 | `SYS_SETUID` | `(uid)` |
| 10 | `SYS_SETGID` | `(gid)` |
| 11 | `SYS_CAP_GET` | `()` → capability mask |
| 12 | `SYS_CAP_DROP` | `(mask)` |
| 13 | `SYS_TASK_INFO` | `(pid, cervus_task_info_t*)` |
| 14 | `SYS_EXECVE` | `(path, argv[], envp[])` |
| 15 | `SYS_GETPGID` | `()` |
| 16 | `SYS_SETPGID` | `(pid, pgid)` |
| 17 | `SYS_GETSID` | `()` |
| 18 | `SYS_SETSID` | `()` |

### I/O & file descriptors (20–33)

| # | Name | Signature |
| :--- | :--- | :--- |
| 20 | `SYS_READ` | `(fd, buf, count)` |
| 21 | `SYS_WRITE` | `(fd, buf, count)` |
| 22 | `SYS_OPEN` | `(path, flags, mode)` |
| 23 | `SYS_CLOSE` | `(fd)` |
| 24 | `SYS_SEEK` | `(fd, offset, whence)` |
| 25 | `SYS_STAT` | `(path, stat*)` |
| 26 | `SYS_FSTAT` | `(fd, stat*)` |
| 27 | `SYS_IOCTL` | `(fd, request, arg)` |
| 28 | `SYS_DUP` | `(fd)` |
| 29 | `SYS_DUP2` | `(oldfd, newfd)` |
| 30 | `SYS_PIPE` | `(int fds[2])` |
| 31 | `SYS_FCNTL` | `(fd, cmd, arg)` |
| 32 | `SYS_READDIR` | `(fd, dirent*)` |
| 33 | `SYS_GETDENTS` | `(fd, buf, count)` |

Open flags (`O_RDONLY`, `O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`,
`O_DIRECTORY`, `O_NONBLOCK`) and `SEEK_SET/CUR/END` are defined in
`kernel/include/fs/vfs.h`. A task has up to `TASK_MAX_FDS = 256` descriptors.

### Memory (40–43)

| # | Name | Signature |
| :--- | :--- | :--- |
| 40 | `SYS_MMAP` | `(hint, length, prot, flags, fd, offset)` |
| 41 | `SYS_MUNMAP` | `(addr, length)` |
| 42 | `SYS_MPROTECT` | `(addr, length, prot)` |
| 43 | `SYS_BRK` | `(new_brk)` |

`prot`: `PROT_NONE/READ/WRITE/EXEC`. `flags`: `MAP_PRIVATE`, `MAP_ANONYMOUS`,
`MAP_FIXED`; failure returns `MAP_FAILED` (`(void*)-1`). See
[Memory Management](Memory-Management#user-memory-brk-and-mmap).

### Time (60–63)

| # | Name | Signature |
| :--- | :--- | :--- |
| 60 | `SYS_CLOCK_GET` | `(clock_id, cervus_timespec_t*)` |
| 61 | `SYS_SLEEP_NS` | `(nanoseconds)` |
| 62 | `SYS_UPTIME` | `()` → ns since boot |
| 63 | `SYS_MEMINFO` | `(cervus_meminfo_t*)` |

Clock ids: `CLOCK_REALTIME (0)`, `CLOCK_MONOTONIC (1)`.

### Futex (80–81)

| # | Name | Signature |
| :--- | :--- | :--- |
| 80 | `SYS_FUTEX_WAIT` | `(uaddr, expected)` |
| 81 | `SYS_FUTEX_WAKE` | `(uaddr, count)` |

### Filesystem (100–114)

| # | Name | Signature |
| :--- | :--- | :--- |
| 100 | `SYS_UNLINK` | `(path)` |
| 101 | `SYS_RMDIR` | `(path)` |
| 102 | `SYS_MKDIR` | `(path, mode)` |
| 103 | `SYS_RENAME` | `(oldpath, newpath)` |
| 104 | `SYS_STATVFS` | `(path, vfs_statvfs_t*)` |
| 105 | `SYS_SYNC` | `()` |
| 106 | `SYS_CHDIR` | `(path)` |
| 107 | `SYS_GETCWD` | `(buf, size)` |
| 108 | `SYS_LIST_MOUNTS` | `(vfs_mount_info_t*, max)` |
| 109 | `SYS_TRUNCATE` | `(path, length)` |
| 110 | `SYS_FTRUNCATE` | `(fd, length)` |
| 111 | `SYS_FSYNC` | `(fd)` |
| 112 | `SYS_FDATASYNC` | `(fd)` |
| 113 | `SYS_SYMLINK` | `(target, linkpath)` |
| 114 | `SYS_READLINK` | `(path, buf, size)` |

### Cervus-specific: system & debug (512–524)

| # | Name | Purpose |
| :--- | :--- | :--- |
| 512 | `SYS_DBG_PRINT` | Write to the kernel serial log (`CAP_DBG_SERIAL`) |
| 513 | `SYS_DBG_DUMP` | Debug dump |
| 514 | `SYS_TASK_SPAWN` | Spawn a task (`CAP_TASK_SPAWN`) |
| 515 | `SYS_TASK_KILL` | Kill a task (`CAP_KILL_ANY` for others) |
| 516–518 | `SYS_SHMEM_*` | Shared-memory create / map / unmap |
| 519–520 | `SYS_IPC_SEND` / `RECV` | Message IPC |
| 521 | `SYS_IOPORT_READ` | `in` from a port (`CAP_IOPORT`) |
| 522 | `SYS_IOPORT_WRITE` | `out` to a port (`CAP_IOPORT`) |
| 523 | `SYS_SHUTDOWN` | Power off (`CAP_REBOOT`) |
| 524 | `SYS_REBOOT` | Reboot (`CAP_REBOOT`) |

### Cervus-specific: disk & partitioning (530–549)

| # | Name | Purpose |
| :--- | :--- | :--- |
| 530 | `SYS_DISK_MOUNT` | Mount a block device at a path |
| 531 | `SYS_DISK_UMOUNT` | Unmount |
| 532 | `SYS_DISK_FORMAT` | Format a partition (`mkfs`) |
| 533 | `SYS_DISK_INFO` | Query disk/partition info |
| 540 | `SYS_DISK_READ_RAW` | Raw sector read |
| 541 | `SYS_DISK_WRITE_RAW` | Raw sector write |
| 542 | `SYS_DISK_PARTITION` | Write an MBR partition table |
| 543 | `SYS_DISK_MKFS_FAT32` | In-kernel FAT32 `mkfs` |
| 544 | `SYS_DISK_LIST_PARTS` | Enumerate partitions (`cervus_part_info_t`) |
| 545 | `SYS_DISK_BIOS_INSTALL` | Install Limine BIOS stage-1 |
| 548 | `SYS_DISK_EJECT` | Eject removable media |
| 549 | `SYS_DISK_PARTITION_GPT` | Write a GPT partition table |

These back the installer and the `mkfs` / `mount` / `fdisk` / `lsblk` utilities.
Most require `CAP_SYSADMIN` / `CAP_FS_ROOT`.

### Cervus-specific: hardware query & console (550–567)

| # | Name | Purpose |
| :--- | :--- | :--- |
| 550 | `SYS_PCI_LIST` | Enumerate PCI devices (`cervus_pci_device_t`) — backs `lspci` |
| 551 | `SYS_USB_LIST` | Enumerate USB devices — backs `lsusb` |
| 553 | `SYS_VT_SPAWN_POLL` | Virtual-terminal spawn poll |
| 554 | `SYS_VT_SET_CTTY` | Set the controlling TTY |
| 555 | `SYS_VT_CLEAR_SHELL` | Clear shell VT state |
| 556 | `SYS_VT_SWITCH` | Switch virtual terminal |
| 560 | `SYS_FB_INFO` | Framebuffer geometry (`cervus_fb_info_t`) |
| 561 | `SYS_FB_BLIT` | Blit a buffer to the framebuffer |
| 562 | `SYS_FB_MAP` | Map the framebuffer into the process |
| 563 | `SYS_FB_ACQUIRE` | Acquire exclusive framebuffer access |
| 564 | `SYS_FB_RELEASE` | Release the framebuffer |
| 565 | `SYS_MOUSE_STATE` | Read mouse state (`cervus_mouse_info_t`) |
| 566 | `SYS_KEYMAP_CONFIG` | Configure the keymap |
| 567 | `SYS_KLOG` | Read the kernel log ring (backs `dmesg`) |

## Shared structures

`syscall_nums.h` also defines the ABI structs passed across the boundary, e.g.
`cervus_task_info_t` (pid/uid/gid/caps/name/state/runtime/rss), `cervus_meminfo_t`,
`cervus_fb_info_t`, `cervus_mouse_info_t`, `cervus_part_info_t` and
`cervus_pci_device_t` (with its `cervus_pci_bar_t[6]`). Userland sees the same
definitions through the sysroot headers under `usr/sysroot/usr/include/sys/`.

<a name="process-image"></a>
## ELF & the process image

New process images are ELF64 loaded by `kernel/src/elf/elf.c`:

* Accepts `ET_EXEC` and position-independent `ET_DYN` (PIE, load base
  `0x0000_0000_0040_0000`).
* Maps each `PT_LOAD` segment with permissions from the program-header flags
  (`PF_R/W/X` → page `WRITE`/`NOEXEC`), zero-filling `.bss`.
* Builds the initial stack (argv/envp/auxv) below
  `0x0000_7FFF_FFFF_E000`, 16-byte aligned per the SysV ABI.
* Sets the task's heap break to the end of the loaded image.

The C runtime entry (`_start`) lives in `usr/lib/libcervus/crt0.asm`; it sets up
the environment and calls `main`, then invokes `SYS_EXIT` with the return value.

---

Next: **[Filesystems](Filesystems)**.
