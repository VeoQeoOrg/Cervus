# Userland

Everything above the kernel — the C library, the init process, the shells, the
editor, the coreutils and the on-device compiler — is in-tree under `usr/`. There
is no external libc and no BusyBox; every binary links against **`libcervus`**.

Sources: `usr/lib/libcervus/`, `usr/apps/`, `usr/bin/`, `usr/installer/`,
`usr/sysroot/`.

## libcervus — the C library

`libcervus` is a POSIX-style C library written from scratch. Its defining
convention: **one function per `.c` file**, grouped into directories by header.
This keeps the static archive's link granularity fine (a program only pulls in the
functions it uses) and makes the source trivially navigable.

| Directory | Contents |
| :--- | :--- |
| `unistd/` | `read`, `write`, `open`, `close`, `fork`, `pipe`, `dup`/`dup2`, `sbrk`, `getpid`, `symlink`, `sleep`/`usleep`, `getopt`, … |
| `stdio/` | `printf`/`scanf` families, buffered `FILE` streams |
| `stdlib/` | `malloc`/`free`, `str→num`, `qsort`, environment, `exit` |
| `string/` | `mem*` and `str*` |
| `ctype/`, `math/` | Character classes, math functions |
| `fcntl/`, `dirent/`, `termios/`, `signal/` | Syscall-backed POSIX wrappers |
| `time/` | Clocks, `sleep`, time conversion |
| `sys/` | `stat`, `mman`, `wait`, `utsname`, `ioctl` wrappers |
| `cervus/` | Cervus-specific syscall wrappers (`syscallN`, `SYS_*`) |
| `internal/` | Private helpers behind a single `<libcervus.h>` |
| `regex/`, `fnmatch/`, `libgen/`, `err/`, `readline/`, `setjmp` | Extra POSIX-ish utilities |

Startup/low-level:

* **`crt0.asm`** — the C runtime entry (`_start`): sets up `argc`/`argv`/`envp`
  and the initial frame, calls `main`, then issues `SYS_EXIT` with the return
  value.
* **`setjmp.asm`** — `setjmp`/`longjmp`.

Private internals are isolated behind a single `<libcervus.h>` header, so the
public API surface (installed into the sysroot) stays clean. Public headers live
in `usr/sysroot/usr/include/` and are installed into the target system so that
on-device compilation works.

The library and its `crt0.o` are archived to
`usr/sysroot/usr/lib/libcervus.a` during the build.

## init and the console

`usr/apps/init.c` is **PID 1** — the first userland process the kernel starts (see
[Boot Process](Boot-Process#4-starting-init)). It:

* Sets up terminal modes (cooked/raw via `termios`).
* Spawns and supervises **12 virtual terminals** (`NVT = 12`), each running a
  login shell — by default `/bin/csh` (the default is configurable, and `chsh`
  changes a user's shell).
* Re-spawns a shell if it exits, keeping the consoles alive.
* Handles disk mounting for a freshly installed system.

Virtual terminals are switched through the `SYS_VT_*` syscalls; `init` owns the
policy of which VT is foreground.

## Shells

Cervus ships an interactive login shell and **`csh`** (`usr/bin/csh.c`), a
C-shell-style interpreter usable both interactively and for scripting. Scripting
features include:

* control flow: `if` / `else` / `endif`, `foreach`, `while`;
* pipes (`|`) and redirects (`>`, `>>`, `<`);
* environment variables and the `$status` exit-code variable;
* command history.

## Applications (`usr/apps/`)

| App | Purpose |
| :--- | :--- |
| `init` | PID 1 — VT/session supervisor |
| `neo` | Modal-free, nano-style text editor |
| `calc` | Interactive calculator |
| `cal` | Calendar |
| `date` | Date/time display |
| `fetch` | System information / "neofetch"-style summary |
| `sysmon` | Live process / system monitor |

## Coreutils (`usr/bin/`)

Around **75** small programs implement a familiar Unix command set. Highlights:

| Category | Commands |
| :--- | :--- |
| Files | `cat`, `ls`, `cp`, `mv`, `rm`, `ln`, `touch`, `mkdir`, `rmdir`, `stat`, `find`, `du`, `df` |
| Text | `grep`, `sed`, `tr`, `cut`, `sort`, `uniq`, `head`, `tail`, `wc`, `tee`, `diff`, `less`, `hexdump`, `printf`, `echo`, `seq`, `factor` |
| Process | `ps`, `top`, `kill`, `killall`, `watch`, `time`, `xargs`, `sleep`, `yes`, `true`, `false` |
| System | `uname`, `uptime`, `whoami`, `id`, `env`, `which`, `tty`, `clear`, `dmesg`, `man`, `reboot`, `shutdown`, `chsh` |
| Hardware | `lspci`, `lsusb`, `lsblk`, `cpuinfo`, `meminfo`, `diskinfo` |
| Disk | `mount`, `umount`, `sync`, `mkfs`, `mkpart`, `fdisk`, `dd`, `wipefs`, `eject`, `tar` |
| Paths | `pwd`, `basename`, `dirname` |
| Framebuffer | `fbdemo` |

Each is a standalone C program linked against `libcervus`; the hardware/disk tools
are thin wrappers over the Cervus-specific [syscalls](System-Calls).

## On-device C compiler (TCC)

A ported **[TCC](https://bellard.org/tcc/)** (Tiny C Compiler) is bundled and
installed into the system (`/usr/bin/tcc` + `libtcc1.a`). Combined with `libcervus`
and the sysroot headers, this makes Cervus **self-hosting for C**: you can write
and compile C programs on the running machine, with no host toolchain involved.

## Installer

`usr/installer/cervus-installer.c` runs only in the Live ISO. It partitions and
formats a target disk (ESP + ext2 root + swap), copies the system tree, and
installs the bootloader — see
[Getting Started → Installing to a disk](Getting-Started#installing-to-a-disk).
It is driven by the disk [syscalls](System-Calls#cervus-specific-disk--partitioning-530549).

---

Next: **[Security Model](Security-Model)**.
