#ifndef SSE_H
#define SSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Регистр управления MXCSR
#define MXCSR_DEFAULT 0x1F80  // Маска по умолчанию
#define MXCSR_FLUSH_TO_ZERO   (1 << 15)
#define MXCSR_DENORMALS_ARE_ZERO (1 << 6)

// Проверка поддержки SSE
bool sse_supported(void);
bool sse2_supported(void);
bool sse3_supported(void);
bool ssse3_supported(void);
bool sse4_1_supported(void);
bool sse4_2_supported(void);
bool avx_supported(void);
bool avx2_supported(void);

// Инициализация SSE
void sse_init(void);

// Установка/получение MXCSR
void sse_set_mxcsr(uint32_t mxcsr);
uint32_t sse_get_mxcsr(void);

// Векторные операции (примеры)
void sse_memcpy_fast(void* dest, const void* src, size_t n);
void sse_memset_fast(void* dest, int value, size_t n);

// Проверка поддержки MMX
bool mmx_supported(void);

// Переключение в режим MMX (вызов перед использованием)
void mmx_enter(void);

// Выход из режима MMX (обязательно после использования!)
void mmx_exit(void);
void test_simd_cpuid_printf(void);

#endif // SSE_H