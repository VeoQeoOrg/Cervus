#ifndef SSE_H
#define SSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MXCSR_DEFAULT 0x1F80
#define MXCSR_FLUSH_TO_ZERO   (1 << 15)
#define MXCSR_DENORMALS_ARE_ZERO (1 << 6)

bool sse_supported(void);
bool sse2_supported(void);
bool sse3_supported(void);
bool ssse3_supported(void);
bool sse4_1_supported(void);
bool sse4_2_supported(void);
bool avx_supported(void);
bool avx2_supported(void);
void sse_init(void);
void sse_set_mxcsr(uint32_t mxcsr);
uint32_t sse_get_mxcsr(void);
void sse_memcpy_fast(void* dest, const void* src, size_t n);
void sse_memset_fast(void* dest, int value, size_t n);
bool mmx_supported(void);
void mmx_enter(void);
void mmx_exit(void);
void print_simd_cpuid(void);

#endif // SSE_H