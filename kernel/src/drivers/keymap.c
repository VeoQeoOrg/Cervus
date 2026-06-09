#include "../include/drivers/keymap.h"

static int g_alt_lang   = KMAP_LANG_NONE;
static int g_toggle_key = KMAP_TOGGLE_ALT_SHIFT;
static int g_active     = 0;

void keymap_toggle(void) {
    if (g_alt_lang == KMAP_LANG_NONE) { g_active = 0; return; }
    g_active = g_active ? 0 : 1;
}

bool keymap_is_alt(void) { return g_active == 1 && g_alt_lang != KMAP_LANG_NONE; }

void keymap_set_config(int alt_lang, int toggle_key) {
    if (alt_lang == KMAP_LANG_NONE || alt_lang == KMAP_LANG_RU)
        g_alt_lang = alt_lang;
    if (toggle_key == KMAP_TOGGLE_ALT_SHIFT || toggle_key == KMAP_TOGGLE_CTRL_SHIFT ||
        toggle_key == KMAP_TOGGLE_CAPSLOCK)
        g_toggle_key = toggle_key;
    if (g_alt_lang == KMAP_LANG_NONE) g_active = 0;
}

int keymap_get_alt_lang(void)   { return g_alt_lang; }
int keymap_get_toggle_key(void) { return g_toggle_key; }

static uint32_t ru_lower(char a) {
    switch (a) {
        case 'q': return 0x0439; case 'w': return 0x0446; case 'e': return 0x0443;
        case 'r': return 0x043A; case 't': return 0x0435; case 'y': return 0x043D;
        case 'u': return 0x0433; case 'i': return 0x0448; case 'o': return 0x0449;
        case 'p': return 0x0437; case '[': return 0x0445; case ']': return 0x044A;
        case 'a': return 0x0444; case 's': return 0x044B; case 'd': return 0x0432;
        case 'f': return 0x0430; case 'g': return 0x043F; case 'h': return 0x0440;
        case 'j': return 0x043E; case 'k': return 0x043B; case 'l': return 0x0434;
        case ';': return 0x0436; case '\'': return 0x044D;
        case 'z': return 0x044F; case 'x': return 0x0447; case 'c': return 0x0441;
        case 'v': return 0x043C; case 'b': return 0x0438; case 'n': return 0x0442;
        case 'm': return 0x044C; case ',': return 0x0431; case '.': return 0x044E;
        case '`': return 0x0451;
        default:  return 0;
    }
}

static uint32_t ru_upper(char a) {
    switch (a) {
        case 'Q': return 0x0419; case 'W': return 0x0426; case 'E': return 0x0423;
        case 'R': return 0x041A; case 'T': return 0x0415; case 'Y': return 0x041D;
        case 'U': return 0x0413; case 'I': return 0x0428; case 'O': return 0x0429;
        case 'P': return 0x0417; case '{': return 0x0425; case '}': return 0x042A;
        case 'A': return 0x0424; case 'S': return 0x042B; case 'D': return 0x0412;
        case 'F': return 0x0410; case 'G': return 0x041F; case 'H': return 0x0420;
        case 'J': return 0x041E; case 'K': return 0x041B; case 'L': return 0x0414;
        case ':': return 0x0416; case '"': return 0x042D;
        case 'Z': return 0x042F; case 'X': return 0x0427; case 'C': return 0x0421;
        case 'V': return 0x041C; case 'B': return 0x0418; case 'N': return 0x0422;
        case 'M': return 0x042C; case '<': return 0x0411; case '>': return 0x042E;
        case '~': return 0x0401;
        default:  return 0;
    }
}

uint32_t keymap_translate(char ascii) {
    if (!keymap_is_alt()) return (uint32_t)(uint8_t)ascii;
    uint32_t cp = ru_lower(ascii);
    if (!cp) cp = ru_upper(ascii);
    if (cp) return cp;
    return (uint32_t)(uint8_t)ascii;
}

void keymap_emit(char ascii, void (*emit)(char)) {
    uint32_t cp = keymap_translate(ascii);
    if (cp < 0x80) {
        emit((char)cp);
    } else if (cp < 0x800) {
        emit((char)(0xC0 | (cp >> 6)));
        emit((char)(0x80 | (cp & 0x3F)));
    } else {
        emit((char)(0xE0 | (cp >> 12)));
        emit((char)(0x80 | ((cp >> 6) & 0x3F)));
        emit((char)(0x80 | (cp & 0x3F)));
    }
}
