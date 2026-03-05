#include "cervus_user.h"

static void uint_to_str(uint64_t v, char* buf, int base) {
    const char* digits = "0123456789abcdef";
    char tmp[32];
    int  i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (v) { tmp[i++] = digits[v % base]; v /= base; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void pr(const char* s) {
     write(1, s, strlen(s));
}

static void prd(uint64_t v)
{
    char b[24];
    uint_to_str(v, b, 10);
    pr(b);
}

static void prx(uint64_t v) {
    pr("0x");
    char b[24];
    uint_to_str(v, b, 16);
    pr(b);
}

static void prln(const char* s) {
    pr(s);
    pr("\n");
}

static void test_identity(void) {
    prln("=== Identity ===");
    pr("  PID:  "); prd(getpid());  pr("\n");
    pr("  PPID: "); prd(getppid()); pr("\n");
    pr("  UID:  "); prd(getuid());  pr("\n");
    pr("  GID:  "); prd(getgid());  pr("\n");
    pr("  CAPS: "); prx(cap_get()); pr("\n");
}

static void test_brk(void) {
    prln("=== BRK / sbrk ===");

    void* cur = sbrk(0);
    pr("  brk initial: "); prx((uint64_t)cur); pr("\n");

    void* mem = sbrk(4096);
    if (mem == (void*)-1) {
        prln("  sbrk(4096) FAILED");
    } else {
        pr("  sbrk(4096) → "); prx((uint64_t)mem); pr("\n");
        volatile char* p = (volatile char*)mem;
        p[0]    = 'C';
        p[4095] = 'V';
        if (p[0] == 'C' && p[4095] == 'V')
            prln("  read/write heap: OK");
        else
            prln("  read/write heap: FAIL");
    }

    void* new_brk = sbrk(0);
    pr("  brk after:   "); prx((uint64_t)new_brk); pr("\n");
}

static void test_mmap(void) {
    prln("=== MMAP ===");

    void* addr = mmap(NULL, 8192,
                      PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      -1, 0);
    if (addr == MAP_FAILED) {
        prln("  mmap FAILED");
        return;
    }
    pr("  mmap(8192) → "); prx((uint64_t)addr); pr("\n");

    volatile uint64_t* p = (volatile uint64_t*)addr;
    p[0] = 0xDEADBEEFCAFEBABEULL;
    if (p[0] == 0xDEADBEEFCAFEBABEULL)
        prln("  write/read: OK");
    else
        prln("  write/read: FAIL");

    if (munmap(addr, 8192) == 0)
        prln("  munmap: OK");
    else
        prln("  munmap: FAIL");
}

static void test_task_info(void) {
    prln("=== TASK_INFO ===");

    cervus_task_info_t info;
    int r = task_info(0, &info);
    if (r < 0) {
        pr("  task_info failed: "); prd((uint64_t)(-r)); pr("\n");
        return;
    }
    pr("  name:  "); pr(info.name);  pr("\n");
    pr("  pid:   "); prd(info.pid);  pr("\n");
    pr("  uid:   "); prd(info.uid);  pr("\n");
    pr("  prio:  "); prd(info.priority); pr("\n");
    pr("  state: "); prd(info.state); pr("\n");
}

static void test_fork_wait(void) {
    prln("=== FORK / WAIT ===");

    pid_t child_pid = fork();

    if (child_pid < 0) {
        prln("  fork FAILED");
        return;
    }

    if (child_pid == 0) {
        pr("  [child] PID="); prd(getpid());
        pr(" PPID=");         prd(getppid()); pr("\n");
        prln("  [child] doing work...");
        volatile uint64_t x = 0;
        for (int i = 0; i < 100000; i++) x += i;
        pr("  [child] result="); prd(x); pr("\n");
        prln("  [child] exiting with code 42");
        exit(42);
    } else {
        pr("  [parent] forked child pid="); prd(child_pid); pr("\n");
        pr("  [parent] waiting...\n");

        int status = 0;
        pid_t waited = waitpid(child_pid, &status, 0);
        if (waited < 0) {
            prln("  [parent] wait returned EAGAIN (blocking wait not yet impl)");
            for (int i = 0; i < 10; i++) {
                yield();
                waited = waitpid(child_pid, &status, WNOHANG);
                if (waited > 0) break;
            }
        }
        if (waited > 0) {
            pr("  [parent] reaped pid="); prd(waited);
            pr(" exit_code="); prd((status >> 8) & 0xFF); pr("\n");
        } else {
            prln("  [parent] child not yet collected (normal — TODO blocking wait)");
        }
    }
}

static void test_cap_drop(void) {
    prln("=== CAP_DROP ===");

    uint64_t before = cap_get();
    pr("  caps before: "); prx(before); pr("\n");

    cap_drop(CAP_DBG_SERIAL);

    uint64_t after = cap_get();
    pr("  caps after:  "); prx(after); pr("\n");

    if ((after & CAP_DBG_SERIAL) == 0)
        prln("  CAP_DBG_SERIAL dropped: OK");
    else
        prln("  CAP_DBG_SERIAL drop: FAIL");
}

void _start(void) {
    prln("=== Cervus OS userspace test ===");
    prln("=== hello from Ring 3!       ===");
    pr("\n");

    test_identity();
    pr("\n");
    test_brk();
    pr("\n");
    test_mmap();
    pr("\n");
    test_task_info();
    pr("\n");
    test_cap_drop();
    pr("\n");
    test_fork_wait();
    pr("\n");

    prln("=== All tests done. Exiting. ===");
    exit(0);
}