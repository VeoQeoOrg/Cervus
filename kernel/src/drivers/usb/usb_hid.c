#include "../../../include/drivers/usb/usb_hid.h"
#include "../../../include/time/clocksource.h"
#include "../../../include/drivers/mouse.h"
#include "../../../include/drivers/keymap.h"
#include "../../../include/apic/apic.h"
#include <string.h>

extern void console_input_char(char c);
extern volatile uint32_t g_ctrlc_pending;

static bool g_caps_lock = false;

static const char hid_kbd_lower[256] = {
    [0x04]='a',[0x05]='b',[0x06]='c',[0x07]='d',[0x08]='e',[0x09]='f',
    [0x0A]='g',[0x0B]='h',[0x0C]='i',[0x0D]='j',[0x0E]='k',[0x0F]='l',
    [0x10]='m',[0x11]='n',[0x12]='o',[0x13]='p',[0x14]='q',[0x15]='r',
    [0x16]='s',[0x17]='t',[0x18]='u',[0x19]='v',[0x1A]='w',[0x1B]='x',
    [0x1C]='y',[0x1D]='z',
    [0x1E]='1',[0x1F]='2',[0x20]='3',[0x21]='4',[0x22]='5',[0x23]='6',
    [0x24]='7',[0x25]='8',[0x26]='9',[0x27]='0',
    [0x28]='\n',[0x29]='\x1b',[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='-',[0x2E]='=',[0x2F]='[',[0x30]=']',[0x31]='\\',
    [0x33]=';',[0x34]='\'',[0x35]='`',[0x36]=',',[0x37]='.',[0x38]='/',
    [0x64]='\\',
};

static const char hid_kbd_upper[256] = {
    [0x04]='A',[0x05]='B',[0x06]='C',[0x07]='D',[0x08]='E',[0x09]='F',
    [0x0A]='G',[0x0B]='H',[0x0C]='I',[0x0D]='J',[0x0E]='K',[0x0F]='L',
    [0x10]='M',[0x11]='N',[0x12]='O',[0x13]='P',[0x14]='Q',[0x15]='R',
    [0x16]='S',[0x17]='T',[0x18]='U',[0x19]='V',[0x1A]='W',[0x1B]='X',
    [0x1C]='Y',[0x1D]='Z',
    [0x1E]='!',[0x1F]='@',[0x20]='#',[0x21]='$',[0x22]='%',[0x23]='^',
    [0x24]='&',[0x25]='*',[0x26]='(',[0x27]=')',
    [0x28]='\n',[0x29]='\x1b',[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='_',[0x2E]='+',[0x2F]='{',[0x30]='}',[0x31]='|',
    [0x33]=':',[0x34]='"',[0x35]='~',[0x36]='<',[0x37]='>',[0x38]='?',
    [0x64]='|',
};

extern void vt_handle_chord(int fn);

extern void input_report_key(int keycode, int pressed);

static const uint8_t g_usage_to_scancode[0x100] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
    [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
    [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
    [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14,
    [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
    [0x26] = 0x0A, [0x27] = 0x0B,
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F,
    [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A,
    [0x30] = 0x1B, [0x31] = 0x2B, [0x33] = 0x27, [0x34] = 0x28,
    [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35,
    [0x39] = 0x3A,
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E,
    [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
    [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,
    [0x49] = 0x52, [0x4A] = 0x47, [0x4B] = 0x49, [0x4C] = 0x53,
    [0x4D] = 0x4F, [0x4E] = 0x51,
    [0x4F] = 0x4D, [0x50] = 0x4B, [0x51] = 0x50, [0x52] = 0x48,
    [0xE0] = 0x1D, [0xE1] = 0x2A, [0xE2] = 0x38, [0xE3] = 0x5B,
    [0xE4] = 0x1D, [0xE5] = 0x36, [0xE6] = 0x38, [0xE7] = 0x5C,
};

static const uint8_t g_usage_to_linux[0x100] = {
      0,   0,   0,   0,  30,  48,  46,  32,  18,  33,  34,  35,  23,  36,  37,  38,
     50,  49,  24,  25,  16,  19,  31,  20,  22,  47,  17,  45,  21,  44,   2,   3,
      4,   5,   6,   7,   8,   9,  10,  11,  28,   1,  14,  15,  57,  12,  13,  26,
     27,  43,  43,  39,  40,  41,  51,  52,  53,  58,  59,  60,  61,  62,  63,  64,
     65,  66,  67,  68,  87,  88,  99,  70, 119, 110, 102, 104, 111, 107, 109, 106,
    105, 108, 103,  69,  98,  55,  74,  78,  96,  79,  80,  81,  75,  76,  77,  71,
     72,  73,  82,  83,  86, 127,
    [0xE0] = 29, [0xE1] = 42, [0xE2] = 56, [0xE3] = 125,
    [0xE4] = 97, [0xE5] = 54, [0xE6] = 100, [0xE7] = 126,
};

extern void input_report_linux_key(int keycode, int pressed);

static void report_evdev(uint8_t usage, int pressed) {
    uint8_t sc = g_usage_to_scancode[usage];
    if (sc) input_report_key(sc, pressed);
    uint8_t lk = g_usage_to_linux[usage];
    if (lk) input_report_linux_key(lk, pressed);
}

static void report_modifiers(uint8_t now, uint8_t was) {
    for (int bit = 0; bit < 8; bit++) {
        uint8_t m = (uint8_t)(1u << bit);
        if ((now & m) == (was & m)) continue;
        report_evdev((uint8_t)(0xE0 + bit), (now & m) ? 1 : 0);
    }
}

static void emit_one(uint8_t usage, uint8_t modifier) {
    bool shift = (modifier & 0x22) != 0;
    bool ctrl  = (modifier & 0x11) != 0;
    bool alt   = (modifier & 0x44) != 0;

    if (ctrl && alt && usage >= 0x3A && usage <= 0x45) {
        vt_handle_chord(usage - 0x3A + 1);
        return;
    }

    if (usage >= 0x4F && usage <= 0x52) {
        console_input_char('\x1b'); console_input_char('[');
        switch (usage) {
            case 0x4F: console_input_char('C'); break;
            case 0x50: console_input_char('D'); break;
            case 0x51: console_input_char('B'); break;
            case 0x52: console_input_char('A'); break;
        }
        return;
    }
    switch (usage) {
        case 0x4A: console_input_char('\x1b'); console_input_char('['); console_input_char('H'); return;
        case 0x4D: console_input_char('\x1b'); console_input_char('['); console_input_char('F'); return;
        case 0x4B: console_input_char('\x1b'); console_input_char('['); console_input_char('5'); console_input_char('~'); return;
        case 0x4E: console_input_char('\x1b'); console_input_char('['); console_input_char('6'); console_input_char('~'); return;
        case 0x4C: console_input_char('\x1b'); console_input_char('['); console_input_char('3'); console_input_char('~'); return;
        case 0x39:
            if (keymap_get_toggle_key() == KMAP_TOGGLE_CAPSLOCK) keymap_toggle();
            else g_caps_lock = !g_caps_lock;
            return;
    }
    if (usage == 0) return;

    bool letter = (usage >= 0x04 && usage <= 0x1D);
    bool use_upper = shift;
    if (letter) use_upper = shift ^ g_caps_lock;

    char base = use_upper ? hid_kbd_upper[usage] : hid_kbd_lower[usage];
    if (base == 0) return;

    if (ctrl && letter) {
        char low = hid_kbd_lower[usage];
        uint8_t ctrl_char = (uint8_t)(low - 'a' + 1);
        if (ctrl_char == 0x03) { g_ctrlc_pending = 1; return; }
        console_input_char((char)ctrl_char);
        return;
    }
    if (keymap_is_alt())
        keymap_emit(base, console_input_char);
    else
        console_input_char(base);
}

void usb_hid_kbd_state_init(usb_hid_kbd_state_t *s) {
    memset(s, 0, sizeof(*s));
}

void usb_hid_kbd_process_report(usb_hid_kbd_state_t *s, const uint8_t *report) {
    uint8_t mod = report[0];
    uint8_t prev_mod = s->prev_report[0];
    int tk = keymap_get_toggle_key();
    if (tk == KMAP_TOGGLE_ALT_SHIFT) {
        bool now_t = (mod & 0x44) && (mod & 0x22);
        bool was_t = (prev_mod & 0x44) && (prev_mod & 0x22);
        if (now_t && !was_t) keymap_toggle();
    } else if (tk == KMAP_TOGGLE_CTRL_SHIFT) {
        bool now_t = (mod & 0x11) && (mod & 0x22);
        bool was_t = (prev_mod & 0x11) && (prev_mod & 0x22);
        if (now_t && !was_t) keymap_toggle();
    }
    s->cur_modifier = mod;
    report_modifiers(mod, prev_mod);
    uint64_t now = clocksource_now_ns();

    for (int i = 2; i < USB_HID_REPORT_LEN; i++) {
        uint8_t u = report[i];
        if (u == 0 || u == 0x01) continue;
        bool already = false;
        for (int j = 2; j < USB_HID_REPORT_LEN; j++) {
            if (s->prev_report[j] == u) { already = true; break; }
        }
        if (already) continue;

        report_evdev(u, 1);
        emit_one(u, mod);
        for (int k = 0; k < USB_HID_MAX_HELD; k++) {
            if (s->held[k].usage == 0) {
                s->held[k].usage          = u;
                s->held[k].first_press_ns = now;
                s->held[k].next_emit_ns   = now + USB_HID_REPEAT_INITIAL_NS;
                break;
            }
        }
    }

    for (int k = 0; k < USB_HID_MAX_HELD; k++) {
        if (s->held[k].usage == 0) continue;
        bool still = false;
        for (int i = 2; i < USB_HID_REPORT_LEN; i++) {
            if (report[i] == s->held[k].usage) { still = true; break; }
        }
        if (!still) s->held[k].usage = 0;
    }

    for (int j = 2; j < USB_HID_REPORT_LEN; j++) {
        uint8_t u = s->prev_report[j];
        if (u == 0 || u == 0x01) continue;
        bool still = false;
        for (int i = 2; i < USB_HID_REPORT_LEN; i++) {
            if (report[i] == u) { still = true; break; }
        }
        if (!still) report_evdev(u, 0);
    }

    memcpy(s->prev_report, report, USB_HID_REPORT_LEN);
}

void usb_hid_kbd_tick_repeats(usb_hid_kbd_state_t **states, int n) {
    uint64_t now = clocksource_now_ns();
    for (int i = 0; i < n; i++) {
        usb_hid_kbd_state_t *s = states[i];
        if (!s) continue;
        for (int k = 0; k < USB_HID_MAX_HELD; k++) {
            if (s->held[k].usage == 0) continue;
            if (now < s->held[k].next_emit_ns) continue;
            emit_one(s->held[k].usage, s->cur_modifier);
            uint64_t target = s->held[k].next_emit_ns + USB_HID_REPEAT_INTERVAL_NS;
            if (target < now) target = now + USB_HID_REPEAT_INTERVAL_NS;
            s->held[k].next_emit_ns = target;
        }
    }
}

void usb_hid_mouse_process_report(const uint8_t *report, int len) {
    if (!report || len < 3) return;

    uint8_t buttons = report[0];
    bool bl = (buttons & 0x01) != 0;
    bool br = (buttons & 0x02) != 0;
    bool bm = (buttons & 0x04) != 0;

    int32_t dx = (int32_t)(int8_t)report[1];
    int32_t dy = (int32_t)(int8_t)report[2];
    int32_t wheel = (len >= 4) ? (int32_t)(int8_t)report[3] : 0;

    mouse_inject_rel(dx, dy, bl, br, bm, wheel);
}
