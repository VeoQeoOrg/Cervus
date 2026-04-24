#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/cervus.h>

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("--- test_mem ---");

    void *brk0 = sbrk(0);
    printf("  initial brk = %p\n", brk0);
    if ((uintptr_t)brk0 > 0) ok("sbrk(0) returns valid address");
    else fail("sbrk(0)");

    void *old = sbrk(4096);
    if (old == brk0) ok("sbrk(4096) returns old brk");
    else fail("sbrk(4096)");

    volatile uint8_t *heap = (volatile uint8_t *)brk0;
    heap[0] = 0xAB; heap[4095] = 0xCD;
    if (heap[0] == 0xAB && heap[4095] == 0xCD) ok("write/read heap page");
    else fail("write/read heap page");

    void *cur = sbrk(0);
    sbrk(-4096);
    void *after = sbrk(0);
    if ((uintptr_t)after == (uintptr_t)cur - 4096) ok("sbrk shrink");
    else fail("sbrk shrink");

    void *m = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m != MAP_FAILED) {
        ok("mmap anonymous 8 KiB");
        volatile uint8_t *p = (volatile uint8_t *)m;
        p[0] = 0x11; p[8191] = 0x22;
        if (p[0] == 0x11 && p[8191] == 0x22) ok("write/read mmap pages");
        else fail("write/read mmap pages");
        if (munmap(m, 8192) == 0) ok("munmap");
        else fail("munmap");
    } else fail("mmap anonymous");

    void *big = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (big != MAP_FAILED) {
        ok("mmap 1 MiB");
        volatile uint8_t *bp = (volatile uint8_t *)big;
        int ok_all = 1;
        for (int i = 0; i < 256; i++) {
            bp[i * 4096] = (uint8_t)i;
            if (bp[i * 4096] != (uint8_t)i) ok_all = 0;
        }
        if (ok_all) ok("touch 256 pages of 1 MiB mmap");
        else fail("touch mmap pages");
        munmap(big, 1024 * 1024);
    } else fail("mmap 1 MiB");

    {
        void *addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (addr != MAP_FAILED) {
            munmap(addr, 4096);
            void *fixed = mmap(addr, 4096, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (fixed == addr) ok("MAP_FIXED at specific address");
            else fail("MAP_FIXED");
            if (fixed != MAP_FAILED) munmap(fixed, 4096);
        }
    }

    puts("--- test_mem done ---");
    return failed ? 1 : 0;
}
