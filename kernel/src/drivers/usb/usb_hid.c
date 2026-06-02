#include "../../../include/drivers/usb/usb_hid.h"
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
};

extern void vt_handle_chord(int fn);

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
        case 0x39: g_caps_lock = !g_caps_lock; return;
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
    console_input_char(base);
}

void usb_hid_kbd_state_init(usb_hid_kbd_state_t *s) {
    memset(s, 0, sizeof(*s));
}

void usb_hid_kbd_process_report(usb_hid_kbd_state_t *s, const uint8_t *report) {
    uint8_t mod = report[0];
    s->cur_modifier = mod;
    uint64_t now = hpet_elapsed_ns();

    for (int i = 2; i < USB_HID_REPORT_LEN; i++) {
        uint8_t u = report[i];
        if (u == 0 || u == 0x01) continue;
        bool already = false;
        for (int j = 2; j < USB_HID_REPORT_LEN; j++) {
            if (s->prev_report[j] == u) { already = true; break; }
        }
        if (already) continue;

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

    memcpy(s->prev_report, report, USB_HID_REPORT_LEN);
}

void usb_hid_kbd_tick_repeats(usb_hid_kbd_state_t **states, int n) {
    uint64_t now = hpet_elapsed_ns();
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
