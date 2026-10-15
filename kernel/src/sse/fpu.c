#include "../../include/sse/fpu.h"
#include "../../include/io/serial.h"
#include <string.h>

#define COM1 0x3F8

#define FXSAVE_AREA_SIZE   512
#define FXSAVE_OFF_FCW     0
#define FXSAVE_OFF_MXCSR   24
#define FPU_DEFAULT_FCW    0x037F
#define FPU_DEFAULT_MXCSR  0x1F80

#define XSAVE_MIN_SIZE     576
#define FPU_AREA_MAX       4096

uint64_t fpu_xcr0;
uint32_t fpu_area_size = FXSAVE_AREA_SIZE;
uint32_t fpu_mxcsr_mask = 0xFFBF;
static bool g_xsave_probed;

static inline void cpuid_count(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "0"(leaf), "2"(sub));
}

static inline void xsetbv0(uint64_t v) {
    asm volatile("xsetbv" :: "c"(0), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)) : "memory");
}

uint32_t fpu_xsave_size_for(uint64_t xcr0) {
    uint32_t size = XSAVE_MIN_SIZE;
    for (int i = 2; i < 63; i++) {
        if (!(xcr0 & (1ull << i))) continue;
        uint32_t a, b, c, d;
        cpuid_count(0xD, (uint32_t)i, &a, &b, &c, &d);
        if (a + b > size) size = a + b;
    }
    return size;
}

static void probe_mxcsr_mask(void) {
    uint8_t fx[512] __attribute__((aligned(16)));
    memset(fx, 0, sizeof fx);
    asm volatile("fxsave64 (%0)" :: "r"(fx) : "memory");
    uint32_t m;
    memcpy(&m, fx + 28, 4);
    fpu_mxcsr_mask = m ? m : 0xFFBF;
}

void fpu_xsave_setup(void) {
    uint32_t a, b, c, d;
    cpuid_count(1, 0, &a, &b, &c, &d);
    if (!(c & (1u << 26))) {
        if (!g_xsave_probed) { probe_mxcsr_mask(); g_xsave_probed = true; }
        return;
    }
    bool avx = (c & (1u << 28)) != 0;

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 1ull << 18;
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    if (!g_xsave_probed) {
        cpuid_count(0xD, 0, &a, &b, &c, &d);
        uint64_t sup = a | ((uint64_t)d << 32);
        uint64_t m = XFEAT_FP | XFEAT_SSE;
        if (avx && (sup & XFEAT_YMM)) m |= XFEAT_YMM;
        uint32_t a7, b7, c7, d7;
        cpuid_count(0, 0, &a7, &b7, &c7, &d7);
        if (a7 >= 7) {
            cpuid_count(7, 0, &a7, &b7, &c7, &d7);
            if ((m & XFEAT_YMM) && (b7 & (1u << 16)) && (sup & XFEAT_AVX512) == XFEAT_AVX512)
                m |= XFEAT_AVX512;
        }
        if (fpu_xsave_size_for(m) > FPU_AREA_MAX) m &= ~XFEAT_AVX512;
        fpu_xcr0 = m;
        fpu_area_size = fpu_xsave_size_for(m);
        probe_mxcsr_mask();
        g_xsave_probed = true;
    }
    xsetbv0(fpu_xcr0);
}

void fpu_state_init(void *state) {
    if (!state) return;
    uint8_t *p = (uint8_t *)state;
    memset(p, 0, fpu_area_size);
    uint16_t fcw   = FPU_DEFAULT_FCW;
    uint32_t mxcsr = FPU_DEFAULT_MXCSR;
    memcpy(p + FXSAVE_OFF_FCW,   &fcw,   sizeof(fcw));
    memcpy(p + FXSAVE_OFF_MXCSR, &mxcsr, sizeof(mxcsr));
}

void fpu_init(void) {
    serial_writestring("[FPU] Initializing x87 FPU...\n");

    if (!fpu_detect()) {
        serial_writestring("[FPU] No FPU detected!\n");
        return;
    }

    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));

    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    cr0 &= ~(1 << 3);
    cr0 |= (1 << 5);
    cr0 |= (1 << 16);

    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    asm volatile("finit");

    fpu_set_control_word(0x037F);

    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);
    cr4 |= (1ULL << 10);
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    uint32_t mxcsr = 0x1F80;
    asm volatile("ldmxcsr %0" : : "m"(mxcsr));

    serial_writestring("[FPU] SSE/SSE2 enabled (CR4.OSFXSR|OSXMMEXCPT)\n");
    serial_writestring("[FPU] FPU initialized successfully\n");
}

bool fpu_detect(void) {
    uint32_t edx;
    asm volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        "mov %%edx, %0\n"
        : "=r"(edx)
        :
        : "eax", "ebx", "ecx", "edx"
    );
    return (edx & (1 << 0)) != 0;
}

void fpu_reset(void) {
    asm volatile("finit");
}

void fpu_set_control_word(uint16_t cw) {
    asm volatile("fldcw %0" : : "m"(cw));
}

uint16_t fpu_get_control_word(void) {
    uint16_t cw;
    asm volatile("fnstcw %0" : "=m"(cw));
    return cw;
}

void fpu_set_status_word(uint16_t sw) {
    (void)sw;
}

uint16_t fpu_get_status_word(void) {
    uint16_t sw;
    asm volatile("fnstsw %0" : "=m"(sw));
    return sw;
}

void fpu_set_tag_word(uint16_t tw) {
    (void)tw;
}

uint16_t fpu_get_tag_word(void) {
    struct {
        uint16_t control_word;
        uint16_t status_word;
        uint16_t tag_word;
        uint16_t fpu_ip;
        uint16_t fpu_cs;
        uint16_t fpu_opcode;
        uint16_t fpu_dp;
        uint16_t fpu_ds;
    } __attribute__((packed)) fpu_env;

    asm volatile("fstenv %0" : "=m"(fpu_env));
    return fpu_env.tag_word;
}