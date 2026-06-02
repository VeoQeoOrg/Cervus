#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static void cpuid_leaf(uint32_t leaf, uint32_t *a, uint32_t *b,
                       uint32_t *c, uint32_t *d)
{
    asm volatile ("cpuid"
                  : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                  : "0"(leaf), "2"(0));
}

static void cpuid_leaf_sub(uint32_t leaf, uint32_t sub,
                           uint32_t *a, uint32_t *b,
                           uint32_t *c, uint32_t *d)
{
    asm volatile ("cpuid"
                  : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                  : "0"(leaf), "2"(sub));
}

static uint64_t read_tsc(void)
{
    uint32_t lo, hi;
    asm volatile ("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t measure_tsc_freq_mhz(void)
{
    uint64_t t0_ns = cervus_uptime_ns();
    uint64_t t0    = read_tsc();
    cervus_nanosleep(100000000ULL);
    uint64_t t1_ns = cervus_uptime_ns();
    uint64_t t1    = read_tsc();

    uint64_t dt_ns  = t1_ns - t0_ns;
    uint64_t dt_tsc = t1 - t0;
    if (dt_ns == 0) return 0;
    return (dt_tsc * 1000ULL) / dt_ns;
}

static void detect_topology(int vendor_intel, int vendor_amd, uint32_t max_basic,
                            uint32_t ext_max,
                            unsigned *out_logical, unsigned *out_cores)
{
    *out_logical = 1;
    *out_cores   = 1;

    unsigned lp = 1;
    uint32_t a, b, c, d;
    if (max_basic >= 1) {
        cpuid_leaf(1, &a, &b, &c, &d);
        lp = (b >> 16) & 0xFF;
        if (lp == 0) lp = 1;
        bool htt = (d & (1u << 28)) != 0;
        if (!htt) lp = 1;
    }
    *out_logical = lp;

    if (vendor_intel && max_basic >= 0xB) {
        cpuid_leaf_sub(0xB, 0, &a, &b, &c, &d);
        unsigned threads_per_core = b & 0xFFFF;
        cpuid_leaf_sub(0xB, 1, &a, &b, &c, &d);
        unsigned threads_per_pkg = b & 0xFFFF;
        if (threads_per_pkg > 0 && threads_per_core > 0) {
            *out_logical = threads_per_pkg;
            *out_cores   = threads_per_pkg / threads_per_core;
            return;
        }
    }

    if (vendor_intel && max_basic >= 4) {
        cpuid_leaf_sub(4, 0, &a, &b, &c, &d);
        unsigned cores_per_pkg = ((a >> 26) & 0x3F) + 1;
        if (cores_per_pkg > 0) {
            *out_cores = cores_per_pkg;
            if (*out_logical < cores_per_pkg) *out_logical = cores_per_pkg;
            return;
        }
    }

    if (vendor_amd && ext_max >= 0x80000008) {
        cpuid_leaf(0x80000008, &a, &b, &c, &d);
        unsigned nc = (c & 0xFF) + 1;
        *out_cores = nc;
        if (*out_logical < nc) *out_logical = nc;
        return;
    }

    *out_cores = *out_logical;
}

static void print_kib_human(uint64_t kib)
{
    if (kib >= 1024 * 1024) {
        uint64_t mib10 = (kib * 10) / 1024;
        printf("%lu.%lu MiB", (unsigned long)(mib10 / 1024 / 10),
               (unsigned long)((mib10 / 1024) % 10));
    } else if (kib >= 1024) {
        printf("%lu KiB", (unsigned long)kib);
    } else {
        printf("%lu KiB", (unsigned long)kib);
    }
}

static const char *cache_type_name(uint32_t t)
{
    switch (t) {
        case 1: return "Data";
        case 2: return "Instruction";
        case 3: return "Unified";
        default: return "?";
    }
}

static void emit_feat(int *first, const char *name)
{
    if (*first) { fputs(name, stdout); *first = 0; }
    else        { putchar(' '); fputs(name, stdout); }
}

static void print_features_basic(uint32_t ecx, uint32_t edx)
{
    int f = 1;
    fputs("  Features (CPUID.1):\n    ", stdout);
    if (edx & (1u <<  0)) emit_feat(&f, "FPU");
    if (edx & (1u <<  4)) emit_feat(&f, "TSC");
    if (edx & (1u <<  5)) emit_feat(&f, "MSR");
    if (edx & (1u <<  6)) emit_feat(&f, "PAE");
    if (edx & (1u <<  8)) emit_feat(&f, "CX8");
    if (edx & (1u <<  9)) emit_feat(&f, "APIC");
    if (edx & (1u << 11)) emit_feat(&f, "SEP");
    if (edx & (1u << 13)) emit_feat(&f, "PGE");
    if (edx & (1u << 15)) emit_feat(&f, "CMOV");
    if (edx & (1u << 17)) emit_feat(&f, "PSE36");
    if (edx & (1u << 19)) emit_feat(&f, "CLFLUSH");
    if (edx & (1u << 23)) emit_feat(&f, "MMX");
    if (edx & (1u << 24)) emit_feat(&f, "FXSR");
    if (edx & (1u << 25)) emit_feat(&f, "SSE");
    if (edx & (1u << 26)) emit_feat(&f, "SSE2");
    if (edx & (1u << 28)) emit_feat(&f, "HTT");
    putchar('\n');
    f = 1;
    fputs("    ", stdout);
    if (ecx & (1u <<  0)) emit_feat(&f, "SSE3");
    if (ecx & (1u <<  1)) emit_feat(&f, "PCLMUL");
    if (ecx & (1u <<  3)) emit_feat(&f, "MONITOR");
    if (ecx & (1u <<  5)) emit_feat(&f, "VMX");
    if (ecx & (1u <<  9)) emit_feat(&f, "SSSE3");
    if (ecx & (1u << 12)) emit_feat(&f, "FMA");
    if (ecx & (1u << 13)) emit_feat(&f, "CMPXCHG16B");
    if (ecx & (1u << 19)) emit_feat(&f, "SSE4.1");
    if (ecx & (1u << 20)) emit_feat(&f, "SSE4.2");
    if (ecx & (1u << 22)) emit_feat(&f, "MOVBE");
    if (ecx & (1u << 23)) emit_feat(&f, "POPCNT");
    if (ecx & (1u << 25)) emit_feat(&f, "AES");
    if (ecx & (1u << 26)) emit_feat(&f, "XSAVE");
    if (ecx & (1u << 28)) emit_feat(&f, "AVX");
    if (ecx & (1u << 29)) emit_feat(&f, "F16C");
    if (ecx & (1u << 30)) emit_feat(&f, "RDRAND");
    if (ecx & (1u << 31)) emit_feat(&f, "HYPERVISOR");
    putchar('\n');
}

static void print_features_ext(uint32_t ebx, uint32_t ecx, uint32_t edx)
{
    (void)ecx;
    int f = 1;
    fputs("  Features (CPUID.7.0):\n    ", stdout);
    if (ebx & (1u <<  0)) emit_feat(&f, "FSGSBASE");
    if (ebx & (1u <<  3)) emit_feat(&f, "BMI1");
    if (ebx & (1u <<  5)) emit_feat(&f, "AVX2");
    if (ebx & (1u <<  7)) emit_feat(&f, "SMEP");
    if (ebx & (1u <<  8)) emit_feat(&f, "BMI2");
    if (ebx & (1u << 16)) emit_feat(&f, "AVX512F");
    if (ebx & (1u << 18)) emit_feat(&f, "RDSEED");
    if (ebx & (1u << 19)) emit_feat(&f, "ADX");
    if (ebx & (1u << 20)) emit_feat(&f, "SMAP");
    if (ebx & (1u << 29)) emit_feat(&f, "SHA");
    if (edx & (1u << 26)) emit_feat(&f, "IBPB");
    if (edx & (1u << 27)) emit_feat(&f, "STIBP");
    if (edx & (1u << 28)) emit_feat(&f, "L1D_FLUSH");
    putchar('\n');
}

static void print_features_ext_amd(uint32_t ecx, uint32_t edx)
{
    int f = 1;
    fputs("  Features (CPUID.80000001h):\n    ", stdout);
    if (edx & (1u << 11)) emit_feat(&f, "SYSCALL");
    if (edx & (1u << 20)) emit_feat(&f, "NX");
    if (edx & (1u << 26)) emit_feat(&f, "1GB-PAGES");
    if (edx & (1u << 27)) emit_feat(&f, "RDTSCP");
    if (edx & (1u << 29)) emit_feat(&f, "LM (long mode)");
    if (ecx & (1u <<  0)) emit_feat(&f, "LAHF/SAHF");
    if (ecx & (1u <<  5)) emit_feat(&f, "LZCNT/ABM");
    if (ecx & (1u <<  8)) emit_feat(&f, "PREFETCHW");
    putchar('\n');
}

static void print_caches_intel(uint32_t base)
{
    uint32_t a, b, c, d;
    fputs("\n  " C_CYAN "Caches" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);
    int any = 0;
    for (uint32_t sub = 0; sub < 16; sub++) {
        cpuid_leaf_sub(base, sub, &a, &b, &c, &d);
        uint32_t type = a & 0x1F;
        if (type == 0) break;
        uint32_t level = (a >> 5) & 0x7;
        uint32_t line  = (b & 0xFFF) + 1;
        uint32_t parts = ((b >> 12) & 0x3FF) + 1;
        uint32_t ways  = ((b >> 22) & 0x3FF) + 1;
        uint32_t sets  = c + 1;
        uint64_t size  = (uint64_t)ways * parts * line * sets;
        printf("  L%u %-11s : ", level, cache_type_name(type));
        print_kib_human(size / 1024);
        printf(", %u-way, %u-byte line\n", ways, line);
        any = 1;
    }
    if (!any) fputs("  (cache topology not reported by CPU)\n", stdout);
}

static void print_caches_amd(void)
{
    uint32_t a, b, c, d;
    fputs("\n  " C_CYAN "Caches" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);
    cpuid_leaf(0x80000005, &a, &b, &c, &d);
    if (c) {
        uint32_t l1d_kb = (c >> 24) & 0xFF;
        uint32_t l1d_line = c & 0xFF;
        printf("  L1 Data        : %u KiB, %u-byte line\n", l1d_kb, l1d_line);
    }
    if (d) {
        uint32_t l1i_kb = (d >> 24) & 0xFF;
        uint32_t l1i_line = d & 0xFF;
        printf("  L1 Instruction : %u KiB, %u-byte line\n", l1i_kb, l1i_line);
    }
    cpuid_leaf(0x80000006, &a, &b, &c, &d);
    if (c) {
        uint32_t l2_kb = (c >> 16) & 0xFFFF;
        uint32_t l2_line = c & 0xFF;
        printf("  L2 Unified     : %u KiB, %u-byte line\n", l2_kb, l2_line);
    }
}

static const char USAGE[] =
    "Usage: cpuinfo\nShow detailed CPU information.\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cpuinfo")) return 0;
    (void)argc; (void)argv;

    uint32_t a, b, c, d;
    putchar('\n');
    fputs("  " C_CYAN "CPU Information" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);

    cpuid_leaf(0, &a, &b, &c, &d);
    char vendor[13];
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';
    uint32_t max_basic = a;
    int is_intel = (memcmp(vendor, "GenuineIntel", 12) == 0);
    int is_amd   = (memcmp(vendor, "AuthenticAMD", 12) == 0);

    printf("  Vendor       : %s\n", vendor);

    uint32_t ext_max = 0;
    cpuid_leaf(0x80000000, &ext_max, &b, &c, &d);
    if (ext_max >= 0x80000004) {
        char brand[49];
        uint32_t *p = (uint32_t *)brand;
        cpuid_leaf(0x80000002, &p[0], &p[1], &p[2],  &p[3]);
        cpuid_leaf(0x80000003, &p[4], &p[5], &p[6],  &p[7]);
        cpuid_leaf(0x80000004, &p[8], &p[9], &p[10], &p[11]);
        brand[48] = '\0';
        const char *br = brand;
        while (*br == ' ') br++;
        printf("  Brand        : %s\n", br);
    }

    if (max_basic >= 1) {
        cpuid_leaf(1, &a, &b, &c, &d);
        uint32_t stepping = a & 0xF;
        uint32_t model    = (a >> 4) & 0xF;
        uint32_t family   = (a >> 8) & 0xF;
        uint32_t ext_mdl  = (a >> 16) & 0xF;
        uint32_t ext_fam  = (a >> 20) & 0xFF;
        if (family == 0xF) family += ext_fam;
        if (family == 6 || family == 0xF) model |= (ext_mdl << 4);
        printf("  Family       : 0x%02x (%u)\n", family, family);
        printf("  Model        : 0x%02x (%u)\n", model, model);
        printf("  Stepping     : %u\n", stepping);
        uint8_t apic_id   = (b >> 24) & 0xFF;
        uint8_t lps_per_pkg = (b >> 16) & 0xFF;
        uint8_t clflush_sz  = ((b >> 8)  & 0xFF) * 8;
        printf("  CLFLUSH line : %u B\n", (unsigned)clflush_sz);
        printf("  LP per pkg   : %u\n", (unsigned)lps_per_pkg);
        printf("  Init APIC ID : %u\n", (unsigned)apic_id);

        unsigned ncores = 1, nthreads = 1;
        detect_topology(is_intel, is_amd, max_basic, ext_max, &nthreads, &ncores);
        printf("  Cores        : %u\n", ncores);
        printf("  Threads      : %u\n", nthreads);
        if (ncores > 0)
            printf("  HT/SMT       : %s (%u thread%s per core)\n",
                   nthreads > ncores ? "yes" : "no",
                   nthreads / ncores, (nthreads / ncores) == 1 ? "" : "s");

        cervus_meminfo_t mi;
        if (cervus_meminfo(&mi) == 0) {
            (void)mi;
        }

        putchar('\n');
        print_features_basic(c, d);
    }

    if (max_basic >= 7) {
        uint32_t a7, b7, c7, d7;
        cpuid_leaf_sub(7, 0, &a7, &b7, &c7, &d7);
        print_features_ext(b7, c7, d7);
    }

    if (ext_max >= 0x80000001) {
        cpuid_leaf(0x80000001, &a, &b, &c, &d);
        print_features_ext_amd(c, d);
    }

    if (ext_max >= 0x80000007) {
        cpuid_leaf(0x80000007, &a, &b, &c, &d);
        if (d & (1u << 8)) {
            fputs("  TSC          : invariant (constant rate)\n", stdout);
        }
    }

    if (ext_max >= 0x80000008) {
        cpuid_leaf(0x80000008, &a, &b, &c, &d);
        uint8_t pa_bits = a & 0xFF;
        uint8_t va_bits = (a >> 8) & 0xFF;
        printf("  Address bits : %u physical / %u virtual\n", pa_bits, va_bits);
    }

    if (is_intel && max_basic >= 4) {
        print_caches_intel(4);
    } else if (is_amd) {
        print_caches_amd();
    }

    fputs("\n  " C_CYAN "Frequency" C_RESET "\n", stdout);
    fputs("  " C_GRAY "-------------------------" C_RESET "\n", stdout);
    if (max_basic >= 0x16) {
        cpuid_leaf(0x16, &a, &b, &c, &d);
        if (a) {
            printf("  Base (16h)   : %u MHz\n", a & 0xFFFF);
            printf("  Max  (16h)   : %u MHz\n", b & 0xFFFF);
            printf("  Bus  (16h)   : %u MHz\n", c & 0xFFFF);
        }
    }
    uint64_t tsc_mhz = measure_tsc_freq_mhz();
    if (tsc_mhz > 0) {
        printf("  TSC measured : %lu MHz (~%lu.%02lu GHz)\n",
               (unsigned long)tsc_mhz,
               (unsigned long)(tsc_mhz / 1000),
               (unsigned long)((tsc_mhz % 1000) / 10));
    }

    putchar('\n');
    return 0;
}
