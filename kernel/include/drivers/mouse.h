#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "ps2.h"

void mouse_set_screen(int32_t w, int32_t h);
void mouse_inject_rel(int32_t dx, int32_t dy, bool btn_left, bool btn_right, bool btn_middle, int32_t wheel);
void mouse_inject_abs(int32_t x, int32_t y, bool btn_left, bool btn_right, bool btn_middle, int32_t wheel);
const mouse_state_t *mouse_get_state(void);

#endif
