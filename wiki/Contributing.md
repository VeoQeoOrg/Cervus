# Contributing

Cervus is a low-level systems project with strict contribution standards. This
page summarises the workflow for the wiki; the authoritative rules live in
[`CONTRIBUTING.md`](../blob/main/CONTRIBUTING.md) at the repository root — read it
before opening a pull request.

> **Understand what you are doing.** Undocumented contributions, PRs that look
> AI-generated, and careless work are rejected. To contribute you must clearly
> understand what your change does and what goal it serves.

## Where you can contribute

* **Kernel** is maintained by [**VeoQeo**](https://github.com/VeoQeo) and requires
  deep kernel expertise.
* **Userspace, libraries, tooling, docs** — open to contributors. Good entry
  points: new `usr/bin` utilities, `libcervus` functions, bug fixes, and
  additional hardware support.

## Build system

The build tool is the single C program `builder/build.c` (compiled to `./build`).
It can also be built via `.fz.yaml` using the **forgezero** utility. See
[Getting Started](Getting-Started) for all commands. Common loop:

```bash
./build run                 # build + boot in QEMU
./build run --no-clean      # keep obj/ between iterations
./build clean               # wipe artifacts
```

## Memory-safety testing ("Alex mode")

Before submitting a PR you **must** verify your changes introduce no memory errors
or leaks. Cervus ships an AddressSanitizer build mode:

```bash
./build alex
```

It rebuilds the whole project — the host builder, kernel, userspace and libraries
— with ASan enabled, runs the full compile + ISO pipeline **without** launching
QEMU, and prints a LeakSanitizer summary of any heap leaks in the build tool on
exit.

## Commit and PR rules

**Commits** must follow [Conventional Commits](https://www.conventionalcommits.org/)
and be **signed off**:

```bash
git commit -s -m "feat: short description"
```

The resulting `Signed-off-by:` line is mandatory — **no sign-off, no merge**. Keep
history clean; malformed commits get rejected or sent back for a rebase.

**Pull requests** must describe:

* **What** was done — the essence of the change.
* **Why** it is needed — the problem it solves.
* **How** it was implemented — a brief technical summary.
* **Proof** — mandatory screenshots, logs, or test output showing it working
  **inside Cervus**.

Without a clear description and hard proof, a PR will not be reviewed.

## Code quality

* Code must be working, clean and tested locally.
* Obvious mistakes, mindless copy-paste, or uncleaned AI-generated comments are
  rejected without explanation.
* Match the surrounding style: the kernel and libc have consistent naming,
  header/source mirroring (`kernel/include/x/` ↔ `kernel/src/x/`), and the
  one-function-per-file convention in `libcervus`.

### A note for kernel changes

The scheduler and syscall fast-paths depend on **fixed struct-field offsets** that
are duplicated as `equ`s in assembly (`sched_asm.asm`, `syscall_asm.asm`) and
guarded by `_Static_assert`s in `kernel/include/sched/sched.h`. If you reorder
fields in `task_t` or the per-CPU area, update the assembly offsets or the build
will fail the asserts. See [Scheduler & SMP](Scheduler-and-SMP#the-task-structure).

## Review

Maintainer **[alexvoste](https://github.com/alexvoste)** reviews every PR for
compliance, correctness and code quality. Respect the process.

---

Back to **[Home](Home)**.
