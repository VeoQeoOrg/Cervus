#ifndef FPU_H
#define FPU_H

#include <stdint.h>
#include <stdbool.h>

#define XFEAT_FP     (1ull << 0)
#define XFEAT_SSE    (1ull << 1)
#define XFEAT_YMM    (1ull << 2)
#define XFEAT_AVX512 (7ull << 5)

void fpu_init(void);
bool fpu_detect(void);
void fpu_set_control_word(uint16_t cw);
uint16_t fpu_get_control_word(void);
void fpu_set_status_word(uint16_t sw);
uint16_t fpu_get_status_word(void);
void fpu_set_tag_word(uint16_t tw);
uint16_t fpu_get_tag_word(void);
void fpu_reset(void);
void fpu_state_init(void *state);
void fpu_xsave_setup(void);
uint32_t fpu_xsave_size_for(uint64_t xcr0);

extern uint64_t fpu_xcr0;
extern uint32_t fpu_area_size;
extern uint32_t fpu_mxcsr_mask;

#endif