#include "../../include/sse/sse.h"
#include "../../include/io/serial.h"
#include "../../include/apic/apic.h"
#include <string.h>
#include <stdio.h>

static uint32_t cpuid_edx = 0;
static uint32_t cpuid_ecx = 0;
static uint32_t cpuid_ebx_ext = 0;
static bool features_cached = false;

static void cpuid_cache_features(void) {
    if (features_cached) return;

    uint32_t eax, ebx, ecx, edx;
    asm volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );
    cpuid_edx = edx;
    cpuid_ecx = ecx;

    uint32_t max_leaf;
    asm volatile("cpuid" : "=a"(max_leaf) : "0"(0) : "ebx", "ecx", "edx");
    if (max_leaf >= 7) {
        asm volatile(
            "mov $7, %%eax\n"
            "xor %%ecx, %%ecx\n"
            "cpuid\n"
            "mov %%ebx, %0\n"
            : "=r"(cpuid_ebx_ext)
            :
            : "eax", "ecx", "edx"
        );
    }

    features_cached = true;
}

bool sse_supported(void)   { cpuid_cache_features(); return (cpuid_edx & (1 << 25)) != 0; }
bool sse2_supported(void)  { cpuid_cache_features(); return (cpuid_edx & (1 << 26)) != 0; }
bool sse3_supported(void)  { cpuid_cache_features(); return (cpuid_ecx & (1 << 0))  != 0; }
bool ssse3_supported(void) { cpuid_cache_features(); return (cpuid_ecx & (1 << 9))  != 0; }
bool sse4_1_supported(void){ cpuid_cache_features(); return (cpuid_ecx & (1 << 19)) != 0; }
bool sse4_2_supported(void){ cpuid_cache_features(); return (cpuid_ecx & (1 << 20)) != 0; }
bool avx_supported(void)   { cpuid_cache_features(); return (cpuid_ecx & (1 << 28)) != 0; }
bool avx2_supported(void)  { cpuid_cache_features(); return (cpuid_ebx_ext & (1 << 5)) != 0; }
bool mmx_supported(void)   { cpuid_cache_features(); return (cpuid_edx & (1 << 23)) != 0; }

void sse_init(void) {
    serial_writestring("[SSE] Initializing SSE/AVX...\n");

    cpuid_cache_features();

    if (!sse_supported()) {
        serial_writestring("[SSE] SSE not supported — skipping SSE/AVX init\n");
        return;
    }

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);

    if (avx_supported()) {
        uint32_t ecx;
        asm volatile(
            "mov $1, %%eax\n"
            "cpuid\n"
            "mov %%ecx, %0\n"
            : "=r"(ecx)
            :
            : "eax", "ebx", "edx"
        );
        if (ecx & (1 << 27)) {
            serial_writestring("[SSE] AVX supported, enabling...\n");
            cr4 |= (1 << 18);
            uint64_t xcr0;
            asm volatile("xgetbv" : "=a"(xcr0) : "c"(0) : "edx");
            xcr0 |= (1 << 1) | (1 << 2);
            asm volatile("xsetbv" : : "c"(0), "a"(xcr0), "d"(xcr0 >> 32));
        }
    }

    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    sse_set_mxcsr(MXCSR_DEFAULT);

    serial_writestring("[SSE] SSE initialized successfully\n");
    if (sse_supported())   serial_writestring("[SSE] SSE:    YES\n");
    if (sse2_supported())  serial_writestring("[SSE] SSE2:   YES\n");
    if (sse3_supported())  serial_writestring("[SSE] SSE3:   YES\n");
    if (sse4_1_supported())serial_writestring("[SSE] SSE4.1: YES\n");
    if (sse4_2_supported())serial_writestring("[SSE] SSE4.2: YES\n");
    if (avx_supported())   serial_writestring("[SSE] AVX:    YES\n");
    if (mmx_supported())   serial_writestring("[SSE] MMX:    YES\n");
}

void sse_set_mxcsr(uint32_t mxcsr) {
    if (!sse_supported()) return;
    asm volatile("ldmxcsr %0" : : "m"(mxcsr));
}

uint32_t sse_get_mxcsr(void) {
    if (!sse_supported()) return 0;
    uint32_t mxcsr;
    asm volatile("stmxcsr %0" : "=m"(mxcsr));
    return mxcsr;
}

void mmx_enter(void) {
    if (!mmx_supported()) return;
}

void mmx_exit(void) {
    if (!mmx_supported()) return;
    asm volatile("emms");
}

void sse_memcpy_fast(void *dest, const void *src, size_t n) {
    if (!sse_supported() || n < 64) {
        memcpy(dest, src, n);
        return;
    }

    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    size_t align_offset = (16 - ((uintptr_t)d & 0xF)) & 0xF;
    if (align_offset > n) align_offset = n;
    for (size_t i = 0; i < align_offset; i++) d[i] = s[i];
    d += align_offset; s += align_offset; n -= align_offset;

    size_t i;
    for (i = 0; i + 64 <= n; i += 64) {
        asm volatile(
            "movdqu (%0), %%xmm0\n"
            "movdqu 16(%0), %%xmm1\n"
            "movdqu 32(%0), %%xmm2\n"
            "movdqu 48(%0), %%xmm3\n"
            "movdqu %%xmm0, (%1)\n"
            "movdqu %%xmm1, 16(%1)\n"
            "movdqu %%xmm2, 32(%1)\n"
            "movdqu %%xmm3, 48(%1)\n"
            :
            : "r"(s + i), "r"(d + i)
            : "xmm0", "xmm1", "xmm2", "xmm3", "memory"
        );
    }
    for (; i < n; i++) d[i] = s[i];
}

void sse_memset_fast(void *dest, int value, size_t n) {
    if (!sse_supported() || n < 64) {
        memset(dest, value, n);
        return;
    }

    uint8_t *d = (uint8_t *)dest;
    uint8_t v = (uint8_t)value;
    uint64_t pattern64 =
        (uint64_t)v << 56 | (uint64_t)v << 48 | (uint64_t)v << 40 | (uint64_t)v << 32 |
        (uint64_t)v << 24 | (uint64_t)v << 16 | (uint64_t)v << 8  | (uint64_t)v;

    size_t align_offset = (16 - ((uintptr_t)d & 0xF)) & 0xF;
    if (align_offset > n) align_offset = n;
    for (size_t i = 0; i < align_offset; i++) d[i] = v;
    d += align_offset; n -= align_offset;

    asm volatile(
        "movq %0, %%xmm0\n"
        "punpcklqdq %%xmm0, %%xmm0\n"
        : : "r"(pattern64) : "xmm0"
    );

    size_t i;
    for (i = 0; i + 64 <= n; i += 64) {
        asm volatile(
            "movdqu %%xmm0, (%0)\n"
            "movdqu %%xmm0, 16(%0)\n"
            "movdqu %%xmm0, 32(%0)\n"
            "movdqu %%xmm0, 48(%0)\n"
            :
            : "r"(d + i)
            : "memory"
        );
    }
    for (; i < n; i++) d[i] = v;
}

void print_simd_cpuid(void) {
    printf("\n=== CPUID/SIMD INFORMATION ===\n");

    uint32_t eax, ebx, ecx, edx;
    char vendor[13] = {0};
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    *(uint32_t *)vendor       = ebx;
    *(uint32_t *)(vendor + 4) = edx;
    *(uint32_t *)(vendor + 8) = ecx;

    printf("CPU Vendor: %s\n", vendor);

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    printf("Processor Signature: 0x%08x\n", eax);
    printf("Stepping: %d  Model: %d  Family: %d\n",
           eax & 0xF, (eax >> 4) & 0xF, (eax >> 8) & 0xF);

    printf("\nSIMD Extensions:\n");
    printf("MMX:    %s\n", (edx & (1 << 23)) ? "YES" : "NO");
    printf("SSE:    %s\n", (edx & (1 << 25)) ? "YES" : "NO");
    printf("SSE2:   %s\n", (edx & (1 << 26)) ? "YES" : "NO");
    printf("SSE3:   %s\n", (ecx & (1 << 0))  ? "YES" : "NO");
    printf("SSSE3:  %s\n", (ecx & (1 << 9))  ? "YES" : "NO");
    printf("SSE4.1: %s\n", (ecx & (1 << 19)) ? "YES" : "NO");
    printf("SSE4.2: %s\n", (ecx & (1 << 20)) ? "YES" : "NO");
    printf("AVX:    %s\n", (ecx & (1 << 28)) ? "YES" : "NO");

    printf("=== END OF CPUID/SIMD INFO ===\n\n");
}

void enable_fsgsbase(void) {
    uint32_t eax = 7, ebx, ecx = 0, edx;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "0"(eax), "2"(ecx));
    if (ebx & (1 << 0)) {
        uint64_t cr4;
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 16);
        asm volatile("mov %0, %%cr4" :: "r"(cr4));
        serial_printf("[FSGSBASE] Enabled on CPU %u\n", lapic_get_id());
    } else {
        serial_writestring("[FSGSBASE] Not supported — using MSR fallback\n");
    }
}