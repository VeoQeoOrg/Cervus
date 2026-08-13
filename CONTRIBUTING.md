# Contributing to Cervus

Thanks for your interest in Cervus. This is a from-scratch operating system —
kernel, C library, drivers, shell, utilities and tooling all live in this one
repository, with no Linux, glibc, or BusyBox underneath. The project values code
that is small enough to be read and understood, and contributions are held to that
standard.

Before anything else: **build it, run it, and understand the part you want to
touch.** A good change starts from knowing what the code already does.

---

## Where you can help

- **Userland** — utilities in `usr/bin`, apps in `usr/apps` (editor `neo`, file
  manager `cfm`, `top`, …), the shell `usr/bin/csh.c`.
- **Libraries** — `usr/lib/libcervus` (libc, crypto, TLS, image decoders, net,
  readline, TUI helpers).
- **Drivers and subsystems** — networking, storage, USB, audio, filesystems under
  `kernel/src`.
- **Tooling and docs** — the `nb` build front-end, `builder/` scripts, this
  documentation.

The **kernel core** (scheduler, memory, syscalls, boot) is maintained by
[VeoQeo](https://github.com/VeoQeo). For non-trivial kernel changes, open an issue
or reach out on the [Telegram channel](https://t.me/veoqeo_off) first so we can
agree on the approach before you invest the time.

---

## Building and running

Cervus builds with a Ninja front-end called `nb`. On first checkout:

```sh
./nb deps            # install build dependencies (auto-detects the distro)
./nb                 # configure + build the kernel, userspace and initramfs
./nb run             # build the ISO and boot it in QEMU
```

Useful `./nb run` options (combine freely):

| Option | Effect |
| --- | --- |
| `--live` | boot the live ISO with no disk |
| `--installed` | boot an existing installed disk (simulate real hardware) |
| `--disk=ide\|ahci\|nvme\|all` | attach disks on the given controller(s) |
| `--net[=e1000\|rtl8139\|virtio]` | attach a NIC with user-mode networking (NAT) |
| `--sound[=hda]` | attach AC97 (or Intel HDA) audio; on by default |
| `--uefi` | boot via UEFI/OVMF instead of BIOS |
| `--res=WxH` | force the framebuffer resolution |

Other targets: `./nb <target>` builds a single ninja target (e.g. `kernel`,
`apps`, `iso`); `./nb reconfigure` regenerates `build.ninja` after adding files;
`./nb clean` removes build artifacts. Run `./nb help` for the full list.

Adding a source file? The builder discovers `kernel/src/**` and `usr/{bin,apps}/*.c`
automatically — just run `./nb reconfigure` (or `./nb`, which reconfigures) so ninja
picks it up.

---

## Code style

- **Match the surrounding code.** Follow the naming, indentation, and idioms of the
  file you are editing rather than importing a different style.
- **C**, freestanding in the kernel, hosted (against libcervus) in userspace.
- **Comments are for the non-obvious.** Explain *why* when the reason isn't clear
  from the code; don't narrate *what* every line does. Do not leave behind
  AI-generated filler comments or dead code.
- Keep changes focused. One logical change per commit; unrelated cleanups go in
  their own commit or PR.

---

## Commits

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(net): add DHCP lease renewal
fix(sched): don't reschedule a zombie task
perf(ext4): batch journal writes
docs: describe the audio stack
```

- Every commit **must be signed off** with `-s`:

  ```sh
  git commit -s -m "fix(vfs): close the fd leak on failed open"
  ```

  This adds the `Signed-off-by:` line certifying you have the right to submit the
  change under the project's license (the Developer Certificate of Origin).
- Write real messages: a one-line summary, and a body explaining the *why* for
  anything non-trivial.
- Keep history clean — rebase and squash noise before opening the PR.

---

## Pull requests

A PR is easiest to review when it explains itself. Include:

- **What** the change does.
- **Why** it's needed — the bug or the feature.
- **How** it works — a short technical summary for anything subtle.
- **Proof it works** — a screenshot, log, or captured output from inside Cervus (a
  QEMU run, a serial log, a screendump). For a bug fix, show the before/after.

Test before you push. The usual loop is `./nb run` in QEMU with the relevant options
(`--net`, `--disk=…`, `--sound`, …). If your change touches hardware behavior, say
what you tested it on.

Pull requests are reviewed by the maintainers before merging. Changes that are
untested, undocumented, or clearly machine-generated without understanding will be
sent back with an explanation of what to fix.

---

## Questions

Open an issue, or join the discussion on the
[Telegram channel](https://t.me/veoqeo_off). If you're unsure whether an idea fits,
ask before building it — it saves everyone time.

Thanks for helping build Cervus.
