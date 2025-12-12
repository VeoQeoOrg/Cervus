#ifndef FPU_H
#define FPU_H

#include <stdint.h>
#include <stdbool.h>

// Инициализация FPU
void fpu_init(void);

// Проверка наличия FPU
bool fpu_detect(void);

// Установить контрольное слово FPU
void fpu_set_control_word(uint16_t cw);

// Получить контрольное слово FPU
uint16_t fpu_get_control_word(void);

// Установить слово статуса FPU
void fpu_set_status_word(uint16_t sw);

// Получить слово статуса FPU
uint16_t fpu_get_status_word(void);

// Установить слово тегов FPU
void fpu_set_tag_word(uint16_t tw);

// Получить слово тегов FPU
uint16_t fpu_get_tag_word(void);

// Сбросить FPU
void fpu_reset(void);

#endif // FPU_H