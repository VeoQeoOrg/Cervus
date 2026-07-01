# Filesystems

Cervus has a **Virtual Filesystem (VFS)** layer that presents a single unified
namespace, with several concrete filesystems mounted underneath it. Two are
on-disk and read/write (**ext2**, **FAT32**); the rest are in-memory or
special-purpose (**ramfs**, **devfs**, **procfs**, **initramfs**, **iso9660**).

Sources: `kernel/src/fs/`, `kernel/include/fs/`.

## The VFS layer

The VFS is built around three objects (`kernel/include/fs/vfs.h`):

* **`vnode_t`** — an in-memory file/directory node. Carries its `type`, `mode`,
  `uid`/`gid`, `size`, `ino`, a refcount, filesystem-private `fs_data`, and a
  pointer to a `vnode_ops_t` operation table.
* **`vnode_ops_t`** — the per-filesystem method table. A filesystem implements
  whichever of these it supports:

  | Group | Methods |
  | :--- | :--- |
  | Data | `read`, `write`, `truncate` |
  | Namespace | `lookup`, `readdir`, `mkdir`, `create`, `unlink`, `rename` |
  | Metadata | `stat`, `ioctl` |
  | Links | `symlink`, `readlink` |
  | Lifetime | `ref`, `unref` |

* **`vfs_mount_t`** — a mount point: a path, a root vnode, the device and fstype
  strings, plus optional `unmount`/`sync` callbacks. Up to
  `VFS_MAX_MOUNTS = 16` mounts.

Node types (`vnode_type_t`): `FILE`, `DIR`, `CHARDEV`, `BLKDEV`, `SYMLINK`,
`PIPE`.

### Path resolution and mounts

`vfs_lookup()` walks a path component by component, crossing mount boundaries
(a vnode with a `mounted` mount transparently redirects into that filesystem's
root). Mount/unmount is done with `vfs_mount()` / `vfs_mount_fs()` /
`vfs_umount()`. `vfs_list_mounts()` (behind `SYS_LIST_MOUNTS`) reports the mount
table.

### Open files and descriptors

* An open file is a `vfs_file_t` (a vnode + current `offset` + `flags` +
  refcount).
* Each task owns an `fd_table_t` mapping small integers to `vfs_file_t*`. The
  table supports up to `TASK_MAX_FDS = 256` descriptors, `dup`/`dup2`, and
  per-fd `FD_CLOEXEC` flags.
* `fork` clones the fd table (`fd_table_clone`); `execve` closes
  close-on-exec descriptors (`fd_table_cloexec`).
* `vfs_init_stdio()` wires fds 0/1/2 to the controlling TTY for a new process.

Core operations: `vfs_open`, `vfs_read`, `vfs_write`, `vfs_seek`, `vfs_stat`,
`vfs_fstat`, `vfs_truncate`/`vfs_ftruncate`, `vfs_fsync`, `vfs_readdir`,
`vfs_mkdir`, `vfs_symlink`/`vfs_readlink`, `vfs_ioctl`, `vfs_statvfs`. These are
what the [file syscalls](System-Calls#io--file-descriptors-2033) call into.

## In-memory & special filesystems

### ramfs

A simple in-memory read/write filesystem. It is the **initial root** at boot
(`ramfs_create_root()` → mounted at `/`), and also the fallback root if no disk
mount succeeds. Directories `/dev /bin /etc /tmp /proc /mnt` are created on it
during boot.

### devfs

Mounted at `/dev`. Exposes character devices:

| Device | Behaviour |
| :--- | :--- |
| `/dev/tty` | Controlling terminal (console I/O) |
| `/dev/null` | Discards writes, EOF on read |
| `/dev/zero` | Infinite zero bytes |

Block devices discovered by the disk layer are also surfaced here for tools like
`lsblk`, `mount` and `mkfs`.

### procfs

Mounted at `/proc`. A synthetic filesystem presenting kernel/process information
as readable files (process list, system stats). It is generated on read, not
stored.

### initramfs

The Live-ISO root. Limine loads an `initramfs.tar` module; `initramfs_mount()`
parses the (USTAR) archive in place and exposes it as a filesystem so the system
can boot fully from RAM with no disk. See [Boot Process](Boot-Process#3-root-device-selection).

### iso9660

Read-only CD/DVD filesystem support (`fs/iso9660.c`), used for reading data from
the boot ISO medium.

## On-disk filesystems

### ext2

Full read/write ext2 (`fs/ext2.c`), used as the **root filesystem of an installed
system**. Capabilities:

* Superblock, block-group descriptors, inode and block bitmaps, indirect blocks.
* Read and write of files and directories; create/unlink/mkdir/rmdir/rename.
* **In-kernel `mkfs.ext2`** — the installer formats the root partition without a
  host tool.

The boot code identifies an installed root by checking the ext2 magic
(`EXT2_SUPER_MAGIC`) at the superblock offset — see
[Boot Process](Boot-Process#3-root-device-selection).

### FAT32

Full read/write FAT32 (`fs/fat32.c`), used for the **EFI System Partition (ESP)**
and general interoperability:

* FAT chain traversal, short and long (VFAT) directory entries.
* Read/write, create/delete, directory operations.
* **In-kernel `mkfs.fat32`** (`SYS_DISK_MKFS_FAT32`).

## Storage stack underneath

Filesystems sit on top of the block layer:

```
VFS  →  filesystem (ext2 / fat32 / iso9660)
     →  partition (MBR / GPT)            drivers/disk/partition.c
     →  blkdev (generic block device)    drivers/disk/blkdev.c
     →  ATA / AHCI / NVMe                drivers/disk/{ata,ahci,nvme}.c
                                          or USB Mass Storage (usb/*/msc.c)
```

See [Device Drivers](Device-Drivers#storage) for the block and controller layers,
and the disk-related [syscalls](System-Calls#cervus-specific-disk--partitioning-530549)
for the raw/format/mount operations exposed to userland.

---

Next: **[Device Drivers](Device-Drivers)**.
