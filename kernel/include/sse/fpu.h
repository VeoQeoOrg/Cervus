#ifndef FPU_H
#define FPU_H

#include <stdint.h>
#include <stdbool.h>

void fpu_init(void);
bool fpu_detect(void);
void fpu_set_control_word(uint16_t cw);
uint16_t fpu_get_control_word(void);
void fpu_set_status_word(uint16_t sw);
uint16_t fpu_get_status_word(void);
void fpu_set_tag_word(uint16_t tw);
uint16_t fpu_get_tag_word(void);
void fpu_reset(void);

#endif // FPU_H