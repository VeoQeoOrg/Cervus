# Security Model

Cervus combines a traditional Unix **UID/GID** identity with a fine-grained
**capability** system. Privilege is not a single root/non-root bit: each task
carries a 64-bit capability mask, and individual sensitive syscalls check for the
specific capability they require.

Source: `kernel/include/sched/capabilities.h`.

## Identity

Each `task_t` has `uid`, `gid`, `pgid` and `sid`. Well-known ids:

| Constant | Value |
| :--- | :--- |
| `UID_ROOT` / `GID_ROOT` | `0` |
| `UID_NOBODY` / `GID_NOBODY` | `65534` |

`setuid` / `setgid` change identity (gated by `CAP_SETUID`); `getuid` / `getgid`
report it. Process-group and session calls (`getpgid`/`setpgid`,
`getsid`/`setsid`) manage job-control relationships.

## Capabilities

A capability is one bit in the task's `capabilities` mask. The kernel checks a
required capability with `cap_has(caps, CAP_X)` before performing a privileged
action, and a task can voluntarily shed privileges with `cap_drop` /
`SYS_CAP_DROP`.

| Capability | Bit | Grants |
| :--- | :---: | :--- |
| `CAP_IOPORT` | 0 | Direct port I/O (`in`/`out`, `SYS_IOPORT_*`) |
| `CAP_RAWMEM` | 1 | Raw physical memory access |
| `CAP_DMA` | 2 | Program DMA |
| `CAP_IRQ` | 3 | Handle/route IRQs |
| `CAP_KILL_ANY` | 4 | Signal/kill tasks you don't own |
| `CAP_SET_PRIO` | 5 | Raise scheduling priority |
| `CAP_TASK_SPAWN` | 6 | Spawn new tasks |
| `CAP_TASK_INFO` | 7 | Inspect other tasks (`SYS_TASK_INFO`) |
| `CAP_MMAP_EXEC` | 8 | Map executable memory |
| `CAP_MMAP_PHYS` | 9 | Map physical addresses |
| `CAP_FS_ROOT` | 10 | Bypass filesystem permission checks |
| `CAP_FS_OWNER` | 11 | Act as owner of any file |
| `CAP_NET_RAW` | 12 | Raw network access *(reserved — no net stack yet)* |
| `CAP_NET_BIND` | 13 | Bind privileged ports *(reserved)* |
| `CAP_SYSADMIN` | 14 | General administration (mount, format, …) |
| `CAP_REBOOT` | 15 | `SYS_SHUTDOWN` / `SYS_REBOOT` |
| `CAP_MODULE` | 16 | Load kernel modules |
| `CAP_SETUID` | 17 | Change UID/GID |
| `CAP_AUDIT` | 18 | Audit subsystem |
| `CAP_PTRACE` | 19 | Trace/inspect other processes |
| `CAP_DBG_SERIAL` | 20 | Write to the kernel serial debug log |

Two aggregate constants and `CAP_ALL` (`~0`) are provided:

```c
#define CAP_BASIC_SET   (CAP_MMAP_EXEC | CAP_TASK_SPAWN)
#define CAP_SERVICE_SET (CAP_BASIC_SET | CAP_TASK_INFO | CAP_SET_PRIO | CAP_NET_BIND)
#define CAP_ALL         (~0ULL)
```

## Initial capabilities

When a process is created, its starting mask is derived from its UID:

```c
static inline uint64_t cap_initial(uint32_t uid) {
    return (uid == UID_ROOT) ? CAP_ALL : CAP_BASIC_SET;
}
```

* **root (UID 0)** starts with **all** capabilities.
* **any other user** starts with only `CAP_BASIC_SET` — enough to spawn tasks and
  map executable memory, but nothing that touches hardware, other processes'
  privacy, or system administration.

## Design notes & privilege dropping

* Because checks are **per-capability**, a service can run as root but immediately
  `SYS_CAP_DROP` everything it does not need, shrinking its attack surface — the
  common "drop privileges after setup" pattern, at capability granularity.
* Capabilities are **monotonically droppable**: a task can remove bits but the
  syscall interface does not let a task grant itself capabilities it lacks.
* `CAP_NET_RAW` / `CAP_NET_BIND` are defined but reserved — the networking stack
  is on the [roadmap](Overview#feature-matrix), not yet implemented.

## Relationship to syscalls

Many Cervus-specific [syscalls](System-Calls#cervus-specific-system--debug-512524)
are capability-gated. Representative mapping:

| Action | Syscalls | Capability |
| :--- | :--- | :--- |
| Port I/O | `SYS_IOPORT_READ/WRITE` | `CAP_IOPORT` |
| Reboot / power off | `SYS_REBOOT`, `SYS_SHUTDOWN` | `CAP_REBOOT` |
| Inspect other tasks | `SYS_TASK_INFO` | `CAP_TASK_INFO` |
| Kill others | `SYS_TASK_KILL` | `CAP_KILL_ANY` |
| Mount / format / partition | `SYS_DISK_*` | `CAP_SYSADMIN` / `CAP_FS_ROOT` |
| Serial debug output | `SYS_DBG_PRINT` | `CAP_DBG_SERIAL` |

---

Next: **[Contributing](Contributing)**.
