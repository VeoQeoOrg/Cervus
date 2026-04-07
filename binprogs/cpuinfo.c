#include "../apps/cervus_user.h"

static void cpuid_leaf(uint32_t leaf,
                       uint32_t *a, uint32_t *b,
                       uint32_t *c, uint32_t *d) {
    asm volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(leaf), "2"(0));
}

CERVUS_MAIN(cpuinfo_main) {
    (void)argc; (void)argv;
    uint32_t a, b, c, d;
    wn();
    ws("  " C_CYAN "CPU Info" C_RESET "\n");
    ws("  " C_GRAY "-------------------------" C_RESET "\n");

    cpuid_leaf(0, &a, &b, &c, &d);
    char vendor[13];
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';
    printf("  Vendor:  %s\n", vendor);
    uint32_t ml = a;

    cpuid_leaf(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        char brand[49]; uint32_t *p = (uint32_t *)brand;
        cpuid_leaf(0x80000002, &p[0], &p[1], &p[2],  &p[3]);
        cpuid_leaf(0x80000003, &p[4], &p[5], &p[6],  &p[7]);
        cpuid_leaf(0x80000004, &p[8], &p[9], &p[10], &p[11]);
        brand[48] = '\0';
        const char *br = brand;
        while (*br == ' ') br++;
        printf("  Brand:   %s\n", br);
    }

    if (ml >= 1) {
        cpuid_leaf(1, &a, &b, &c, &d);
        uint32_t mdl = (a >> 4) & 0xF, fam = (a >> 8) & 0xF;
        if (fam == 0xF) fam += (a >> 20) & 0xFF;
        if (fam == 6 || fam == 0xF) mdl += ((a >> 16) & 0xF) << 4;
        printf("  Family:  %u\n", (unsigned)fam);
        printf("  Model:   %u\n", (unsigned)mdl);
        ws("  Features:");
        if (d & (1u << 25)) ws(" SSE");
        if (d & (1u << 26)) ws(" SSE2");
        if (c & (1u << 0))  ws(" SSE3");
        if (c & (1u << 19)) ws(" SSE4.1");
        if (c & (1u << 28)) ws(" AVX");
        wn();
    }
    wn();
    exit(0);
}