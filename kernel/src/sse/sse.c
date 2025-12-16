#include "../../include/sse/sse.h"
#include "../../include/io/serial.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define COM1 0x3F8

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
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        : "=r"(eax), "=r"(ebx), "=r"(ecx), "=r"(edx)
        :
        : "eax", "ebx", "ecx", "edx"
    );
    
    cpuid_edx = edx;
    cpuid_ecx = ecx;
    
    asm volatile(
        "mov $0, %%eax\n"
        "cpuid\n"
        : "=a"(eax)
        :
        : "ebx", "ecx", "edx"
    );
    
    if (eax >= 7) {
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

bool sse_supported(void) {
    cpuid_cache_features();
    return (cpuid_edx & (1 << 25)) != 0;  // SSE
}

bool sse2_supported(void) {
    cpuid_cache_features();
    return (cpuid_edx & (1 << 26)) != 0;  // SSE2
}

bool sse3_supported(void) {
    cpuid_cache_features();
    return (cpuid_ecx & (1 << 0)) != 0;   // SSE3
}

bool ssse3_supported(void) {
    cpuid_cache_features();
    return (cpuid_ecx & (1 << 9)) != 0;   // SSSE3
}

bool sse4_1_supported(void) {
    cpuid_cache_features();
    return (cpuid_ecx & (1 << 19)) != 0;  // SSE4.1
}

bool sse4_2_supported(void) {
    cpuid_cache_features();
    return (cpuid_ecx & (1 << 20)) != 0;  // SSE4.2
}

bool avx_supported(void) {
    cpuid_cache_features();
    return (cpuid_ecx & (1 << 28)) != 0;  // AVX
}

bool avx2_supported(void) {
    cpuid_cache_features();
    return (cpuid_ebx_ext & (1 << 5)) != 0;  // AVX2
}

bool mmx_supported(void) {
    cpuid_cache_features();
    return (cpuid_edx & (1 << 23)) != 0;  // MMX
}

void sse_init(void) {
    serial_writestring(COM1, "[SSE] Initializing SSE/AVX...\n");
    
    cpuid_cache_features();
    
    if (!sse_supported()) {
        serial_writestring(COM1, "[SSE] SSE not supported!\n");
        return;
    }
    
    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    
    if (avx_supported()) {
        serial_writestring(COM1, "[SSE] AVX supported, enabling...\n");
        
        cr4 |= (1 << 18);  
        
        uint64_t xcr0;
        asm volatile("xgetbv" : "=a"(xcr0) : "c"(0) : "edx");
        xcr0 |= (1 << 1) | (1 << 2);
        asm volatile("xsetbv" : : "c"(0), "a"(xcr0), "d"(xcr0 >> 32));
    }
    
    asm volatile("mov %0, %%cr4" : : "r"(cr4));
    
    sse_set_mxcsr(MXCSR_DEFAULT);
    
    serial_writestring(COM1, "[SSE] SSE initialized successfully\n");
    
    if (sse_supported()) serial_writestring(COM1, "[SSE] SSE: YES\n");
    if (sse2_supported()) serial_writestring(COM1, "[SSE] SSE2: YES\n");
    if (sse3_supported()) serial_writestring(COM1, "[SSE] SSE3: YES\n");
    if (sse4_1_supported()) serial_writestring(COM1, "[SSE] SSE4.1: YES\n");
    if (sse4_2_supported()) serial_writestring(COM1, "[SSE] SSE4.2: YES\n");
    if (avx_supported()) serial_writestring(COM1, "[SSE] AVX: YES\n");
    if (mmx_supported()) serial_writestring(COM1, "[SSE] MMX: YES\n");
}

void sse_set_mxcsr(uint32_t mxcsr) {
    asm volatile("ldmxcsr %0" : : "m"(mxcsr));
}

uint32_t sse_get_mxcsr(void) {
    uint32_t mxcsr;
    asm volatile("stmxcsr %0" : "=m"(mxcsr));
    return mxcsr;
}

void mmx_enter(void) {
    if (!mmx_supported()) return;
    // Ничего не нужно делать для входа в MMX
}

void mmx_exit(void) {
    if (!mmx_supported()) return;
    asm volatile("emms"); 
}

void sse_memcpy_fast(void* dest, const void* src, size_t n) {
    if (!sse_supported() || n < 64) {
        memcpy(dest, src, n);
        return;
    }
    
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    size_t align_offset = (16 - ((uintptr_t)d & 0xF)) & 0xF;
    if (align_offset > n) align_offset = n;
    
    for (size_t i = 0; i < align_offset; i++) {
        d[i] = s[i];
    }
    
    d += align_offset;
    s += align_offset;
    n -= align_offset;
    
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
    
    for (; i < n; i++) {
        d[i] = s[i];
    }
}

void sse_memset_fast(void* dest, int value, size_t n) {
    if (!sse_supported() || n < 64) {
        memset(dest, value, n);
        return;
    }
    
    uint8_t* d = (uint8_t*)dest;
    uint8_t v = (uint8_t)value;
    
    uint64_t pattern64 = (uint64_t)v << 56 | (uint64_t)v << 48 |
                        (uint64_t)v << 40 | (uint64_t)v << 32 |
                        (uint64_t)v << 24 | (uint64_t)v << 16 |
                        (uint64_t)v << 8 | (uint64_t)v;
    
    size_t align_offset = (16 - ((uintptr_t)d & 0xF)) & 0xF;
    if (align_offset > n) align_offset = n;
    
    for (size_t i = 0; i < align_offset; i++) {
        d[i] = v;
    }
    
    d += align_offset;
    n -= align_offset;
    
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
    
    for (; i < n; i++) {
        d[i] = v;
    }
}

void test_simd_cpuid_printf(void) {
    printf("\n=== CPUID/SIMD INFORMATION ===\n");
    
    uint32_t eax, ebx, ecx, edx;
    char vendor[13] = {0};
    
    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    
    *(uint32_t*)vendor = ebx;
    *(uint32_t*)(vendor + 4) = edx;
    *(uint32_t*)(vendor + 8) = ecx;
    vendor[12] = '\0';
    
    printf("CPU Vendor: %s\n", vendor);
    
    asm volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    printf("Processor Signature: 0x%08x\n", eax);
    printf("Stepping: %d\n", eax & 0xF);
    printf("Model: %d\n", (eax >> 4) & 0xF);
    printf("Family: %d\n", (eax >> 8) & 0xF);
    
    printf("\nSIMD Extensions:\n");
    printf("MMX:       %s\n", (edx & (1 << 23)) ? "YES" : "NO");
    printf("SSE:       %s\n", (edx & (1 << 25)) ? "YES" : "NO");
    printf("SSE2:      %s\n", (edx & (1 << 26)) ? "YES" : "NO");
    printf("SSE3:      %s\n", (ecx & (1 << 0)) ? "YES" : "NO");
    printf("SSSE3:     %s\n", (ecx & (1 << 9)) ? "YES" : "NO");
    printf("SSE4.1:    %s\n", (ecx & (1 << 19)) ? "YES" : "NO");
    printf("SSE4.2:    %s\n", (ecx & (1 << 20)) ? "YES" : "NO");
    printf("AVX:       %s\n", (ecx & (1 << 28)) ? "YES" : "NO");
    printf("FMA:       %s\n", (ecx & (1 << 12)) ? "YES" : "NO");
    
    printf("\nFPU Test:\n");
    float PI = 3.14159f;
    float E = 2.71828f;
    float result_fpu = PI + E;
    printf("PI + e = %f\n", result_fpu);
    
    if (edx & (1 << 25)) {
        printf("\nSSE Vector Test:\n");
        float vec1[4] = {1.5f, 2.5f, 3.5f, 4.5f};
        float vec2[4] = {0.5f, 1.5f, 2.5f, 3.5f};
        float vec_result[4] = {0};
        
        asm volatile(
            "movups %1, %%xmm0\n"
            "movups %2, %%xmm1\n"
            "addps %%xmm1, %%xmm0\n"
            "movups %%xmm0, %0\n"
            : "=m"(vec_result)
            : "m"(vec1), "m"(vec2)
            : "xmm0", "xmm1", "memory"
        );
        
        printf("[%.2f, %.2f, %.2f, %.2f] + [%.2f, %.2f, %.2f, %.2f] =\n",
               vec1[0], vec1[1], vec1[2], vec1[3],
               vec2[0], vec2[1], vec2[2], vec2[3]);
        printf("[%.2f, %.2f, %.2f, %.2f]\n",
               vec_result[0], vec_result[1], 
               vec_result[2], vec_result[3]);

        double pi = 3.141592653589793;
        double large = 123456.789;
        double small = 0.0000123456;

        printf("pi = %f\n", pi);           // 3.141593
        printf("pi = %.10f\n", pi);        // 3.1415926536
        printf("large = %e\n", large);     // 1.234568e+05
        printf("small = %e\n", small);     // 1.234560e-05
        printf("auto = %g\n", pi);         // 3.14159
        printf("auto = %g\n", small);      // 1.23456e-05

        printf("inf = %f\n", INFINITY);    // inf
        printf("nan = %f\n", NAN);         // nan
        
    }
    
    printf("=== END OF CPUID/SIMD INFO ===\n\n");
}