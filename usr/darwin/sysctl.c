#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/utsname.h>
#include "darwin.h"

enum kind { K_INT, K_QUAD, K_STR, K_FN };

struct dctl {
    const char *name;
    int mib[4];
    int miblen;
    enum kind kind;
    long long (*num)(void);
    const char *(*str)(void);
    int (*fn)(void *out, size_t *len);
};

static long long ncpu(void) { long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? n : 1; }
static long long pagesz(void) { return sysconf(_SC_PAGESIZE); }
static long long memsize(void) { return (long long)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE); }
static long long one(void) { return 1; }
static long long zero(void) { return 0; }
static long long byteorder(void) { return 1234; }
static long long cacheline(void) { return 64; }
static long long l1(void) { return 32768; }
static long long l2(void) { return 262144; }
static long long l3(void) { return 8388608; }
static long long argmax(void) { return 1048576; }
static long long maxfiles(void) { return 122880; }
static long long maxfilesproc(void) { return 61440; }
static long long maxproc(void) { return 4176; }
static long long osrev(void) { return 199506; }
static long long cpufreq(void) { return 2400000000ll; }
static long long cpufamily(void) { return 0x38435547; }

static void cpuid(unsigned leaf, unsigned sub, unsigned *r)
{
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

static int has_feature(unsigned leaf, unsigned sub, int reg, unsigned bit)
{
    unsigned r[4];
    cpuid(leaf & 0x80000000u, 0, r);
    if (r[0] < leaf)
        return 0;
    cpuid(leaf, sub, r);
    return (r[reg] >> bit) & 1;
}

static long long f_sse3(void) { return has_feature(1, 0, 2, 0); }
static long long f_ssse3(void) { return has_feature(1, 0, 2, 9); }
static long long f_sse41(void) { return has_feature(1, 0, 2, 19); }
static long long f_sse42(void) { return has_feature(1, 0, 2, 20); }
static long long f_avx(void) { return has_feature(1, 0, 2, 28); }
static long long f_avx2(void) { return has_feature(7, 0, 1, 5); }
static long long f_popcnt(void) { return has_feature(1, 0, 2, 23); }
static long long f_aes(void) { return has_feature(1, 0, 2, 25); }
static long long f_bmi1(void) { return has_feature(7, 0, 1, 3); }
static long long f_bmi2(void) { return has_feature(7, 0, 1, 8); }
static long long f_fma(void) { return has_feature(1, 0, 2, 12); }
static long long f_f16c(void) { return has_feature(1, 0, 2, 29); }

static const char *s_ostype(void) { return "Darwin"; }
static const char *s_osrelease(void) { return "23.6.0"; }
static const char *s_osversion(void) { return "23G93"; }
static const char *s_product(void) { return "14.6.1"; }
static const char *s_machine(void) { return "x86_64"; }
static const char *s_model(void) { return "Cervus1,1"; }

static const char *s_version(void)
{
    static char b[256];
    struct utsname u;
    uname(&u);
    snprintf(b, sizeof b, "Darwin Kernel Version 23.6.0: %s %s; %s", u.sysname, u.release, u.version);
    return b;
}

static const char *s_hostname(void)
{
    static char b[256];
    if (gethostname(b, sizeof b) < 0)
        strcpy(b, "localhost");
    return b;
}

static const char *s_brand(void)
{
    static char b[49];
    unsigned r[12];
    cpuid(0x80000000u, 0, r);
    if (r[0] >= 0x80000004u) {
        cpuid(0x80000002u, 0, r);
        cpuid(0x80000003u, 0, r + 4);
        cpuid(0x80000004u, 0, r + 8);
        memcpy(b, r, 48);
        b[48] = 0;
        char *p = b;
        while (*p == ' ')
            p++;
        return p;
    }
    return "x86_64 CPU";
}

static const char *s_vendor(void)
{
    static char b[13];
    unsigned r[4], v[3];
    cpuid(0, 0, r);
    v[0] = r[1];
    v[1] = r[3];
    v[2] = r[2];
    memcpy(b, v, 12);
    b[12] = 0;
    return b;
}

static int fn_boottime(void *out, size_t *len)
{
    struct { long sec; int usec; int pad; } tv;
    struct timespec now, up;
    clock_gettime(CLOCK_REALTIME, &now);
    clock_gettime(CLOCK_MONOTONIC, &up);
    tv.sec = now.tv_sec - up.tv_sec;
    tv.usec = 0;
    tv.pad = 0;
    if (out) {
        if (*len < sizeof tv) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(out, &tv, sizeof tv);
    }
    *len = sizeof tv;
    return 0;
}

static int fn_loadavg(void *out, size_t *len)
{
    struct { uint32_t l[3]; long fscale; } la = { { 0, 0, 0 }, 2048 };
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        double v[3];
        if (fscanf(f, "%lf %lf %lf", &v[0], &v[1], &v[2]) == 3)
            for (int i = 0; i < 3; i++)
                la.l[i] = (uint32_t)(v[i] * 2048);
        fclose(f);
    }
    if (out) {
        if (*len < sizeof la) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(out, &la, sizeof la);
    }
    *len = sizeof la;
    return 0;
}

static int fn_clockrate(void *out, size_t *len)
{
    int ci[5] = { 100, 10000, 0, 100, 100 };
    if (out) {
        if (*len < sizeof ci) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(out, ci, sizeof ci);
    }
    *len = sizeof ci;
    return 0;
}

static int fn_swapusage(void *out, size_t *len)
{
    struct { uint64_t total, avail, used; uint32_t pagesize; int encrypted; } xs = { 0, 0, 0, 4096, 0 };
    if (out) {
        if (*len < sizeof xs) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(out, &xs, sizeof xs);
    }
    *len = sizeof xs;
    return 0;
}

#define I(n, a, b, f) { n, { a, b }, 2, K_INT, f, NULL, NULL }
#define Q(n, a, b, f) { n, { a, b }, 2, K_QUAD, f, NULL, NULL }
#define S(n, a, b, f) { n, { a, b }, 2, K_STR, NULL, f, NULL }
#define F(n, a, b, f) { n, { a, b }, 2, K_FN, NULL, NULL, f }
#define IN(n, f) { n, { 0 }, 0, K_INT, f, NULL, NULL }
#define QN(n, f) { n, { 0 }, 0, K_QUAD, f, NULL, NULL }
#define SN(n, f) { n, { 0 }, 0, K_STR, NULL, f, NULL }
#define FN(n, f) { n, { 0 }, 0, K_FN, NULL, NULL, f }

static const struct dctl g_ctl[] = {
    S("kern.ostype", 1, 1, s_ostype),
    S("kern.osrelease", 1, 2, s_osrelease),
    I("kern.osrevision", 1, 3, osrev),
    S("kern.version", 1, 4, s_version),
    I("kern.maxproc", 1, 6, maxproc),
    I("kern.maxfiles", 1, 7, maxfiles),
    I("kern.argmax", 1, 8, argmax),
    S("kern.hostname", 1, 10, s_hostname),
    F("kern.clockrate", 1, 12, fn_clockrate),
    F("kern.boottime", 1, 21, fn_boottime),
    I("kern.maxfilesperproc", 1, 29, maxfilesproc),
    S("kern.osversion", 1, 65, s_osversion),
    SN("kern.osproductversion", s_product),
    SN("kern.osproductversioncompat", s_product),
    IN("kern.hv_support", zero),
    IN("kern.secure_kernel", zero),
    IN("sysctl.proc_translated", zero),
    IN("sysctl.proc_native", one),
    S("hw.machine", 6, 1, s_machine),
    S("hw.model", 6, 2, s_model),
    I("hw.ncpu", 6, 3, ncpu),
    I("hw.byteorder", 6, 4, byteorder),
    I("hw.physmem", 6, 5, memsize),
    I("hw.pagesize", 6, 7, pagesz),
    I("hw.cpufrequency", 6, 15, cpufreq),
    I("hw.cachelinesize", 6, 16, cacheline),
    I("hw.l1icachesize", 6, 17, l1),
    I("hw.l1dcachesize", 6, 18, l1),
    I("hw.l2cachesize", 6, 20, l2),
    I("hw.l3cachesize", 6, 22, l3),
    Q("hw.memsize", 6, 24, memsize),
    I("hw.activecpu", 6, 25, ncpu),
    IN("hw.logicalcpu", ncpu),
    IN("hw.logicalcpu_max", ncpu),
    IN("hw.physicalcpu", ncpu),
    IN("hw.physicalcpu_max", ncpu),
    IN("hw.packages", one),
    QN("hw.cpufrequency_max", cpufreq),
    IN("hw.cpufamily", cpufamily),
    IN("hw.optional.sse", one),
    IN("hw.optional.sse2", one),
    IN("hw.optional.sse3", f_sse3),
    IN("hw.optional.supplementalsse3", f_ssse3),
    IN("hw.optional.sse4_1", f_sse41),
    IN("hw.optional.sse4_2", f_sse42),
    IN("hw.optional.avx1_0", f_avx),
    IN("hw.optional.avx2_0", f_avx2),
    IN("hw.optional.popcnt", f_popcnt),
    IN("hw.optional.aes", f_aes),
    IN("hw.optional.bmi1", f_bmi1),
    IN("hw.optional.bmi2", f_bmi2),
    IN("hw.optional.fma", f_fma),
    IN("hw.optional.f16c", f_f16c),
    IN("hw.optional.x86_64", one),
    IN("hw.optional.arm64", zero),
    IN("hw.optional.avx512f", zero),
    SN("machdep.cpu.brand_string", s_brand),
    SN("machdep.cpu.vendor", s_vendor),
    IN("machdep.cpu.core_count", ncpu),
    IN("machdep.cpu.thread_count", ncpu),
    F("vm.loadavg", 2, 2, fn_loadavg),
    F("vm.swapusage", 2, 5, fn_swapusage),
    IN("vm.pagesize", pagesz),
};

#define NCTL (sizeof g_ctl / sizeof g_ctl[0])

static int emit(const struct dctl *c, void *out, size_t *len)
{
    if (c->kind == K_FN)
        return c->fn(out, len);
    if (c->kind == K_STR) {
        const char *s = c->str();
        size_t n = strlen(s) + 1;
        if (out) {
            if (*len < n) {
                memcpy(out, s, *len);
                errno = ENOMEM;
                return -1;
            }
            memcpy(out, s, n);
        }
        *len = n;
        return 0;
    }
    long long v = c->num();
    size_t n = c->kind == K_QUAD ? 8 : 4;
    if (out) {
        if (*len < n) {
            if (*len == 8 && n == 4) {
                memcpy(out, &v, 8);
                *len = 8;
                return 0;
            }
            errno = ENOMEM;
            return -1;
        }
        if (n == 8 || *len == 8) {
            memcpy(out, &v, 8);
            n = 8;
        } else {
            int32_t i = (int32_t)v;
            memcpy(out, &i, 4);
        }
    }
    *len = n;
    return 0;
}

static const struct dctl *by_name(const char *n)
{
    for (size_t i = 0; i < NCTL; i++)
        if (!strcmp(g_ctl[i].name, n))
            return &g_ctl[i];
    return NULL;
}

static const struct dctl *by_mib(const int *mib, unsigned n)
{
    if (n < 2)
        return NULL;
    for (size_t i = 0; i < NCTL; i++)
        if (g_ctl[i].miblen && g_ctl[i].mib[0] == mib[0] && g_ctl[i].mib[1] == mib[1])
            return &g_ctl[i];
    if (mib[0] == 0x7fff && n >= 2 && mib[1] >= 1000 && mib[1] < 1000 + (int)NCTL)
        return &g_ctl[mib[1] - 1000];
    return NULL;
}

static int d_sysctlbyname(const char *name, void *out, size_t *len, const void *nv, size_t nl)
{
    const struct dctl *c = by_name(name);
    if (!c) {
        errno = ENOENT;
        return -1;
    }
    if (nv || nl) {
        errno = EPERM;
        return -1;
    }
    return emit(c, out, len);
}

static int d_sysctl(const int *mib, unsigned n, void *out, size_t *len, const void *nv, size_t nl)
{
    if (n >= 2 && mib[0] == 0 && mib[1] == 3) {
        const char *name = nv;
        const struct dctl *c = name ? by_name(name) : NULL;
        if (!c) {
            errno = ENOENT;
            return -1;
        }
        int m[2];
        if (c->miblen) {
            m[0] = c->mib[0];
            m[1] = c->mib[1];
        } else {
            m[0] = 0x7fff;
            m[1] = 1000 + (int)(c - g_ctl);
        }
        size_t want = sizeof m;
        if (out) {
            if (*len < want) {
                errno = ENOMEM;
                return -1;
            }
            memcpy(out, m, want);
        }
        *len = want;
        (void)nl;
        return 0;
    }
    if (n >= 3 && mib[0] == 1 && (mib[1] == 14 || mib[1] == 49 || mib[1] == 38))
        return d_sysctl_proc(mib, n, out, len);
    if (n >= 6 && mib[0] == 4 && mib[1] == 17)
        return d_sysctl_route(mib, n, out, len);
    const struct dctl *c = by_mib(mib, n);
    if (!c) {
        errno = ENOENT;
        return -1;
    }
    if (nv || nl) {
        errno = EPERM;
        return -1;
    }
    return emit(c, out, len);
}

static int d_sysctlnametomib(const char *name, int *mib, size_t *n)
{
    const struct dctl *c = by_name(name);
    if (!c) {
        errno = ENOENT;
        return -1;
    }
    if (*n < 2) {
        errno = ENOMEM;
        return -1;
    }
    if (c->miblen) {
        mib[0] = c->mib[0];
        mib[1] = c->mib[1];
    } else {
        mib[0] = 0x7fff;
        mib[1] = 1000 + (int)(c - g_ctl);
    }
    *n = 2;
    return 0;
}

DSYM("_sysctl", d_sysctl);
DSYM("_sysctlbyname", d_sysctlbyname);
DSYM("_sysctlnametomib", d_sysctlnametomib);
