# ⚙️ Contributing to CervusOS

You can contribute to CervusOS easily — just follow a few strict rules, and everything will be perfect.

CervusOS is, first and foremost, a low‑level project.  
We do **not** tolerate undocumented contributions, PRs that appear AI‑generated, or careless work.

Most importantly: to contribute you must **clearly understand** what you are doing and what goal your feature or fix serves.

**The kernel** is maintained by [**VeoQeo**](https://github.com/VeoQeo) 
You can contribute to userspace, libraries, tooling, or any other part of the OS that does not require deep kernel expertise.

---

## Build system

We use a custom builder located at `builder/build.c`.  

* Build it via `.fz.yaml` (install the **forgezero** utility first: [forgezero‑cli/forgezero](https://github.com/forgezero-cli/forgezero)).
* Run `./builder` and choose the desired option.  
The workflow is straightforward and fully self‑contained.

## Memory Safety Testing (Alex Mode)

Before submitting a Pull Request, you **must** verify that your changes do not introduce memory errors or leaks.  
CervusOS provides an automated AddressSanitizer (ASan) test mode for this purpose.

```bash
./build alex
# Or using interactive menu
```

### What it does:

-> Rebuilds the entire project — host‑side builder, kernel, userspace applications, and libraries — with AddressSanitizer enabled.

-> Runs a full compilation and ISO creation pipeline without launching QEMU.

-> At exit, LeakSanitizer prints a summary of all heap memory leaks detected in the build tool itself.

Expected output after a clean run

---

## Rules for commits and Pull Requests in CervusOS

### 1. Commit formatting

Every commit in your PR **must** follow the [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) standard.

* Read and apply the specification.
* **Every commit MUST be signed** with the `-s` flag:  
  `git commit -s -m "feat: short description"`  
  The resulting `Signed-off-by: Your Name <email>` line is **mandatory** to certify your authorship.  
  **No signature → PR is discarded immediately.**
* Respect the commit history.  
  Malformed commits mean automatic rejection or a demand to rebase.

### 2. Pull Request requirements

Every PR must be described clearly and completely:

* **What was done** – the essence of your feature or bug fix.
* **Why it is needed** – the problem or task this code solves in the OS.
* **How it was implemented** – a brief technical summary of your solution.
* **Proof** – **mandatory** screenshots, logs, or test output that demonstrate correct operation inside CervusOS.

Without a sensible description and hard proof, the code will not even be reviewed.

### 3. Code quality and AI

* Code must be working, clean, and tested locally.
* Obvious mistakes, mindless copy‑pasting from ChatGPT, or uncleaned AI‑generated comments → immediate rejection with no explanation.  
  We value **your brain**, not a AI context window.

If your code fails to meet open‑source standards or looks like it was written by a careless amateur, expect **direct, public criticism**.  
Snowflakes are not welcome here — we are building an operating system.

---

Maintainer **[alexvoste](https://github.com/alexvoste)** reviews every PR for compliance, correctness, and code quality.  
Thank you for respecting the process.