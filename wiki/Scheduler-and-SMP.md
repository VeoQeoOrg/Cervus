# Scheduler & SMP

Cervus runs a **preemptive, priority-based** scheduler across multiple CPUs. Each
CPU keeps its own current task and per-CPU state; tasks are drawn from shared
priority run-queues. This page covers the task model, scheduling policy, context
switching, SMP bring-up and the process lifecycle (`fork`/`exec`/`wait`).

Sources: `kernel/src/sched/{sched.c,sched_asm.asm}`, `kernel/src/smp/`,
`kernel/include/sched/sched.h`.

## The task structure

Every thread of execution — kernel task or userland process — is a `task_t`
(`kernel/include/sched/sched.h`). Key fields:

| Field | Purpose |
| :--- | :--- |
| `rsp`, `rip`, `rbp_save`, `cr3` | Saved machine context (offset-checked in asm) |
| `pid`, `ppid`, `uid`, `gid` | Identity |
| `pgid`, `sid` | Process group / session |
| `capabilities` | Capability bitmask (see [Security Model](Security-Model)) |
| `state` | `RUNNING` / `READY` / `BLOCKED` / `ZOMBIE` / `DEAD` |
| `priority` | `0`–`31` (`DEFAULT_PRIORITY = 16`) |
| `time_slice`, `time_slice_init` | Preemption budget (`TASK_DEFAULT_TIMESLICE = 10`) |
| `pagemap`, `brk_*` | Address space + heap break |
| `fpu_state`, `fpu_used` | Lazy FPU save area |
| `fd_table` | Open file descriptors (see [Filesystems](Filesystems)) |
| `parent`, `children`, `sibling` | Process tree |
| `cwd`, `ctty` | Working directory, controlling TTY |
| `cpu_affinity`, `last_cpu` | SMP placement hints |
| `wakeup_time_ns` | Deadline for sleeping tasks |

The header contains `_Static_assert`s pinning critical field offsets (`rsp`,
`entry`, `user_rsp`, `user_saved_rip`, …) because the assembly context-switch and
syscall paths reference them by fixed byte offset. **If you reorder `task_t`, the
asserts will fail the build** — update the corresponding `equ`s in
`sched_asm.asm` / `syscall_asm.asm`.

## Scheduling policy

* There are **32 priority levels** (`0`–`MAX_PRIORITY = 31`). Each level has its
  own ready queue: `ready_queues[MAX_PRIORITY + 1]`.
* The scheduler picks the highest-priority non-empty queue and runs tasks within a
  level **round-robin**, each for up to its `time_slice` ticks.
* The **APIC timer** decrements the running task's slice on every tick; at zero it
  flags `need_resched` on the per-CPU area. The reschedule is taken at the next
  safe boundary (IRQ return / syscall return), never mid-critical-section.
* `current_task[MAX_CPUS]` holds the running task per CPU; `sched_reschedule()`
  performs selection and the switch.

Cooperative and blocking primitives:

```c
void task_yield(void);            // give up the CPU voluntarily (SYS_YIELD)
void task_sleep_ns(uint64_t ns);  // block until a deadline
void task_sleep_ms(uint64_t ms);
void task_unblock(task_t* t);     // move BLOCKED -> READY
```

Sleeping tasks are woken by `sched_wakeup_sleepers(now_ns)` when their
`wakeup_time_ns` deadline passes.

## Context switching

The switch itself is in `sched/sched_asm.asm`:

* `context_switch(old, next, current_task_slot, new_cr3)` — saves callee-saved
  registers and `rsp` into `old`, loads them from `next`, swaps `CR3` if the
  address space changed, and updates the per-CPU `current_task` slot.
* `first_task_start(task)` — jumps into a brand-new task the first time it runs.
* Trampolines set up the correct initial frame per task kind:
  * `task_trampoline` — kernel task (`entry(arg)`);
  * `task_trampoline_user` — ring-3 entry (drops to user mode with `iretq`);
  * `task_trampoline_fork` — a forked child resuming as if it returned from the
    syscall.

FPU state is saved/restored lazily: only tasks that touched the FPU
(`fpu_used`) pay the `FXSAVE`/`FXRSTOR` cost.

## SMP (multicore)

`smp_init(mp_response)` uses the Limine MP response to bring up the **application
processors (APs)**:

* Each AP is started via LAPIC INIT–SIPI IPIs and runs a per-CPU init path
  (its own GDT/TSS load, IDT, FPU/SSE enable, LAPIC setup).
* **Per-CPU data** (`kernel/src/smp/percpu.c`) is reached through the `GS` base
  (`FSGSBASE`). It holds the kernel stack pointer, the current task, the saved
  user register scratch used by the syscall path, and the `need_resched` flag.
  The offsets are mirrored as `equ`s in `syscall_asm.asm`.
* `kernel_main` waits up to 5 seconds for `smp_get_online_count()` to reach
  `cpu_count - 1` before continuing; a timeout is logged and boot proceeds.

Counters: `smp_get_cpu_count()` (configured), `smp_get_online_count()` (booted).

## Process lifecycle

Cervus implements a Unix-style process model on top of the task layer:

| Operation | Syscall | Behaviour |
| :--- | :--- | :--- |
| Create | `SYS_FORK` | Copies the parent: clones the pagemap, fd table (with close-on-exec handling) and task state; child returns `0`, parent gets the child PID. |
| Replace image | `SYS_EXECVE` | Loads a new ELF into a fresh address space, resets the heap break, keeps the fd table (minus `FD_CLOEXEC`), and restarts at the new entry. |
| Terminate | `SYS_EXIT` / `SYS_EXIT_GROUP` | Task becomes a `ZOMBIE`; children are re-parented. |
| Reap | `SYS_WAIT` | Parent collects a child's exit code (`WNOHANG` supported); frees the zombie. |

Supporting operations: `task_reparent()`, `task_kill()` /
`task_kill_subtree()`, `task_wakeup_waiters()`, and process-group / session
syscalls (`getpgid`/`setpgid`/`getsid`/`setsid`). A foreground task is tracked per
controlling TTY (`task_find_foreground()` / `task_set_foreground()`) so the shell
can do job-control-style signalling.

### IPC and synchronisation

* **Pipes** — `SYS_PIPE` creates a unidirectional byte pipe as a pair of vnodes;
  blocking read/write with EOF on last-writer close.
* **Futexes** — `SYS_FUTEX_WAIT` / `SYS_FUTEX_WAKE` provide the primitive for
  userland mutexes/condition variables.
* **Signals** — a basic signal facility is present (delivery on
  kernel→user transitions).
* Spinlocks (`kernel/include/sched/spinlock.h`) and the small `atomic_bool`
  helpers in `sched.h` guard shared scheduler structures on SMP.

---

Next: **[System Calls](System-Calls)**.
