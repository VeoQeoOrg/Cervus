#ifndef DRIVERS_KEYMAP_H
#define DRIVERS_KEYMAP_H

#include <stdint.h>
#include <stdbool.h>

#define KMAP_LANG_NONE  0
#define KMAP_LANG_RU    1

#define KMAP_TOGGLE_ALT_SHIFT   0
#define KMAP_TOGGLE_CTRL_SHIFT  1
#define KMAP_TOGGLE_CAPSLOCK    2

void     keymap_toggle(void);
bool     keymap_is_alt(void);
uint32_t keymap_translate(char ascii);
void     keymap_emit(char ascii, void (*emit)(char));

void     keymap_set_config(int alt_lang, int toggle_key);
int      keymap_get_alt_lang(void);
int      keymap_get_toggle_key(void);

#endif
