#include "../../include/drivers/ps2.h"
#include "../../include/sched/sched.h"
#include "../../include/fs/devfs.h"
#include "../../include/drivers/timer.h"
#include "../../include/interrupts/interrupts.h"
#include "../../include/apic/apic.h"
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"
#include "../../include/graphics/fb/fb.h"
#include <stdio.h>
#include <stddef.h>

#define KB_IRQ_VECTOR    0x21
#define MOUSE_IRQ_VECTOR 0x2C
#define KB_IRQ_LINE      1
#define MOUSE_IRQ_LINE   12

typedef enum { MOUSE_MODE_RELATIVE = 0, MOUSE_MODE_ABSOLUTE } mouse_mode_t;

static volatile kb_state_t    kb_state;
static volatile mouse_state_t mouse_state;
static volatile mouse_mode_t  mouse_mode       = MOUSE_MODE_RELATIVE;
static volatile int32_t       mouse_screen_w   = 1024;
static volatile int32_t       mouse_screen_h   = 768;

static volatile uint8_t mouse_packet[4];
static volatile uint8_t mouse_packet_idx = 0;
static volatile bool    mouse_has_scroll = false;
static volatile uint32_t mouse_lost_sync = 0;

static volatile kb_buf_t kb_buf;

static const char sc_lower[89] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,
};

static const char sc_upper[89] = {
    0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,
};


static void ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) return;
        io_wait();
    }
}

static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) return;
        io_wait();
    }
}

static void ps2_send_cmd(uint8_t cmd)   { ps2_wait_write(); outb(PS2_CMD_PORT,  cmd);  }
static void ps2_send_data(uint8_t data) { ps2_wait_write(); outb(PS2_DATA_PORT, data); }
static uint8_t ps2_recv_data(void)      { ps2_wait_read();  return inb(PS2_DATA_PORT); }

static void ps2_flush_output(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)) break;
        inb(PS2_DATA_PORT);
        io_wait();
    }
}

static bool ps2_mouse_wait_ack(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            uint8_t r = inb(PS2_DATA_PORT);
            if (r == 0xFA) return true;
            if (r == 0xFE || r == 0xFC) return false;
        }
        io_wait();
    }
    serial_writestring("[PS2] mouse_wait_ack: TIMEOUT\n");
    return false;
}

static bool ps2_mouse_cmd(uint8_t cmd) {
    ps2_send_cmd(PS2_CMD_WRITE_PORT2);
    ps2_send_data(cmd);
    return ps2_mouse_wait_ack();
}

static bool ps2_mouse_cmd_param(uint8_t cmd, uint8_t param) {
    return ps2_mouse_cmd(cmd) && ps2_mouse_cmd(param);
}

static uint8_t ps2_mouse_read_byte(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)
            return inb(PS2_DATA_PORT);
        io_wait();
    }
    return 0xFF;
}

static uint8_t ps2_mouse_get_id(void) {
    ps2_mouse_cmd(0xF2);
    return ps2_mouse_read_byte();
}

static bool ps2_mouse_init_scroll(void) {
    ps2_mouse_cmd_param(0xF3, 200);
    ps2_mouse_cmd_param(0xF3, 100);
    ps2_mouse_cmd_param(0xF3,  80);
    uint8_t id = ps2_mouse_get_id();
    serial_printf("[PS2] Mouse ID after IntelliMouse knock: 0x%02x\n", id);
    return (id == 0x03);
}

static bool ps2_detect_absolute(void) {
    uint8_t id = ps2_mouse_get_id();
    serial_printf("[PS2] Mouse base device ID: 0x%02x\n", id);

    if (!ps2_mouse_cmd(0xE9)) {
        serial_writestring("[PS2] Status request failed\n");
        return false;
    }

    uint8_t status      = ps2_mouse_read_byte();
    uint8_t resolution  = ps2_mouse_read_byte();
    uint8_t sample_rate = ps2_mouse_read_byte();
    serial_printf("[PS2] Mouse status: flags=0x%02x res=0x%02x rate=%d\n",
                  status, resolution, sample_rate);

    if (resolution == 0x08) {
        serial_writestring("[PS2] Detected: QEMU absolute tablet mode\n");
        return true;
    }

    ps2_mouse_cmd_param(0xF3, 200); ps2_mouse_cmd_param(0xF3, 100); ps2_mouse_cmd_param(0xF3, 80);
    ps2_mouse_cmd_param(0xF3, 200); ps2_mouse_cmd_param(0xF3, 100); ps2_mouse_cmd_param(0xF3, 80);

    id = ps2_mouse_get_id();
    serial_printf("[PS2] Mouse ID after abs knock: 0x%02x\n", id);
    if (id == 0x08) {
        serial_writestring("[PS2] Detected: QEMU absolute mode (ID=0x08)\n");
        return true;
    }

    return false;
}

static char scancode_to_char(uint8_t sc) {
    if (sc >= sizeof(sc_lower) / sizeof(sc_lower[0])) return 0;
    bool use_upper = kb_state.shift;
    if ((sc >= 0x10 && sc <= 0x19) ||
        (sc >= 0x1E && sc <= 0x26) ||
        (sc >= 0x2C && sc <= 0x32))
        use_upper = kb_state.caps_lock ^ kb_state.shift;
    return use_upper ? sc_upper[sc] : sc_lower[sc];
}

void kb_buf_push(char c) {
    uint8_t next = (kb_buf.tail + 1) % KB_BUF_SIZE;
    if (next != kb_buf.head) {
        kb_buf.buf[kb_buf.tail] = c;
        kb_buf.tail = next;
    }
}

extern uint32_t cursor_x;
extern uint32_t cursor_y;
extern uint32_t bg_color;
extern struct limine_framebuffer *global_framebuffer;
extern void     get_cursor_position(uint32_t *x, uint32_t *y);
extern uint32_t get_screen_width(void);

static void mouse_print_status(void) {
    const mouse_state_t *m = (const mouse_state_t *)&mouse_state;
    const char *scroll_str = "NONE";
    if (m->scroll == MOUSE_SCROLL_UP)   scroll_str = "UP  ";
    if (m->scroll == MOUSE_SCROLL_DOWN) scroll_str = "DOWN";

    serial_printf("[MOUSE] X:%5d Y:%5d  L:%d M:%d R:%d  Scroll:%s\n",
                  m->x, m->y,
                  (int)m->btn_left, (int)m->btn_middle, (int)m->btn_right,
                  scroll_str);

    uint32_t saved_x, saved_y;
    get_cursor_position(&saved_x, &saved_y);

    if (global_framebuffer)
        fb_fill_rect(global_framebuffer, 0, 0, global_framebuffer->width, 16, bg_color);

    cursor_x = 0; cursor_y = 0;
    printf("[MOUSE] X:%-5d Y:%-5d  [L:%d M:%d R:%d]  Wheel:%s",
           m->x, m->y,
           (int)m->btn_left, (int)m->btn_middle, (int)m->btn_right,
           scroll_str);

    cursor_x = saved_x;
    cursor_y = saved_y;
}

DEFINE_IRQ(KB_IRQ_VECTOR, ps2_kb_handler)
{
    (void)frame;
    static bool e0_prefix = false;
    uint8_t sc       = inb(PS2_DATA_PORT);
    bool    released = (sc & PS2_KEY_RELEASE_BIT) != 0;
    uint8_t key      = sc & ~PS2_KEY_RELEASE_BIT;

    if (sc == 0xE0) { e0_prefix = true; lapic_eoi(); return; }

    if (e0_prefix) {
        e0_prefix = false;
        if (!released) {
            switch (key) {
                case 0x48: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('A'); break;
                case 0x50: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('B'); break;
                case 0x4D: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('C'); break;
                case 0x4B: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('D'); break;
                case 0x47: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('H'); break;
                case 0x4F: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('F'); break;
                case 0x53: kb_buf_push('\x1b'); kb_buf_push('['); kb_buf_push('3'); kb_buf_push('~'); break;
                default: break;
            }
        }
        lapic_eoi();
        return;
    }

    if (key == SC_LSHIFT || key == SC_RSHIFT) { kb_state.shift     = !released; lapic_eoi(); return; }
    if (key == SC_LCTRL)                       { kb_state.ctrl      = !released; lapic_eoi(); return; }
    if (key == SC_LALT)                        { kb_state.alt       = !released; lapic_eoi(); return; }
    if (key == SC_CAPS && !released)           { kb_state.caps_lock = !kb_state.caps_lock; lapic_eoi(); return; }
    if (released)                              { lapic_eoi(); return; }

    if (kb_state.ctrl) {
        char base = scancode_to_char(key);
        serial_printf("[PS2] ctrl+key sc=0x%02x base='%c'(0x%02x)\n", (unsigned)key, (base>=0x20&&base<0x7f)?base:'?', (unsigned)(uint8_t)base);
        if (base >= 'a' && base <= 'z') {
            uint8_t ctrl_char = (uint8_t)(base - 'a' + 1);
            serial_printf("[PS2] ctrl char generated: 0x%02x\n", ctrl_char);
            if (ctrl_char == 0x03) {
                g_ctrlc_pending = 1;
                lapic_eoi();
                return;
            }
            kb_buf_push((char)ctrl_char);
            lapic_eoi();
            return;
        }
        if (base >= 'A' && base <= 'Z') {
            kb_buf_push((char)(base - 'A' + 1));
            lapic_eoi();
            return;
        }
    }

    char c = scancode_to_char(key);
    if (c != 0)
        kb_buf_push(c);

    lapic_eoi();
}

DEFINE_IRQ(MOUSE_IRQ_VECTOR, ps2_mouse_handler)
{
    (void)frame;
    uint8_t data = inb(PS2_DATA_PORT);
    uint8_t idx  = mouse_packet_idx;

    if (idx == 0 && !(data & 0x08)) {
        mouse_lost_sync++;
        lapic_eoi();
        return;
    }

    mouse_packet[idx] = data;
    mouse_packet_idx  = (uint8_t)(idx + 1);

    uint8_t pkt_size = mouse_has_scroll ? 4 : 3;
    if (mouse_packet_idx < pkt_size) { lapic_eoi(); return; }
    mouse_packet_idx = 0;

    uint8_t flags = mouse_packet[0];
    if ((flags & MOUSE_X_OVERFLOW) || (flags & MOUSE_Y_OVERFLOW)) { lapic_eoi(); return; }

    mouse_state.btn_left   = (flags & MOUSE_BTN_LEFT)   != 0;
    mouse_state.btn_right  = (flags & MOUSE_BTN_RIGHT)  != 0;
    mouse_state.btn_middle = (flags & MOUSE_BTN_MIDDLE) != 0;

    if (mouse_mode == MOUSE_MODE_ABSOLUTE) {
        uint16_t abs_x = (uint16_t)mouse_packet[1] | ((flags & 0x10) ? 0x100 : 0);
        uint16_t abs_y = (uint16_t)mouse_packet[2] | ((flags & 0x20) ? 0x100 : 0);
        mouse_state.x  = (int32_t)((uint32_t)abs_x * (uint32_t)mouse_screen_w / 0x1FF);
        mouse_state.y  = (int32_t)((uint32_t)abs_y * (uint32_t)mouse_screen_h / 0x1FF);
    } else {
        int32_t dx = mouse_packet[1];
        int32_t dy = mouse_packet[2];
        if (flags & MOUSE_X_SIGN) dx |= 0xFFFFFF00;
        if (flags & MOUSE_Y_SIGN) dy |= 0xFFFFFF00;
        mouse_state.x += dx;
        mouse_state.y -= dy;
    }

    if (mouse_state.x < 0)               mouse_state.x = 0;
    if (mouse_state.y < 0)               mouse_state.y = 0;
    if (mouse_state.x >= mouse_screen_w) mouse_state.x = mouse_screen_w - 1;
    if (mouse_state.y >= mouse_screen_h) mouse_state.y = mouse_screen_h - 1;

    if (mouse_has_scroll) {
        uint8_t z_raw = mouse_packet[3] & 0x0F;
        int8_t  z     = (z_raw & 0x08) ? (int8_t)(z_raw | 0xF0) : (int8_t)z_raw;
        if      (z > 0) mouse_state.scroll = MOUSE_SCROLL_DOWN;
        else if (z < 0) mouse_state.scroll = MOUSE_SCROLL_UP;
        else            mouse_state.scroll = MOUSE_SCROLL_NONE;
    }

    mouse_print_status();
    lapic_eoi();
}

bool ps2_init(void) {
    serial_writestring("[PS2] Initializing PS/2 driver...\n");

    if (!apic_is_available()) {
        serial_writestring("[PS2] ERROR: APIC not available\n");
        return false;
    }

    serial_writestring("[PS2] Initializing PS/2 controller...\n");
    ps2_send_cmd(PS2_CMD_DISABLE_PORT1);
    ps2_send_cmd(PS2_CMD_DISABLE_PORT2);
    ps2_flush_output();

    ps2_send_cmd(PS2_CMD_READ_CONFIG);
    uint8_t cfg = ps2_recv_data();
    cfg &= ~(PS2_CFG_PORT1_IRQ | PS2_CFG_PORT2_IRQ | PS2_CFG_PORT1_XLAT);
    ps2_send_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(cfg);

    ps2_send_cmd(PS2_CMD_SELF_TEST);
    uint8_t result = ps2_recv_data();
    if (result != 0x55) { serial_printf("[PS2] Self-test FAILED (0x%02x)\n", result); return false; }
    serial_writestring("[PS2] Self-test PASSED\n");
    ps2_send_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(cfg);

    ps2_send_cmd(PS2_CMD_TEST_PORT1);
    bool kb_ok = (ps2_recv_data() == 0x00);
    serial_printf("[PS2] Port 1 (keyboard): %s\n", kb_ok ? "OK" : "FAIL");

    ps2_send_cmd(PS2_CMD_TEST_PORT2);
    bool mouse_ok = (ps2_recv_data() == 0x00);
    serial_printf("[PS2] Port 2 (mouse):    %s\n", mouse_ok ? "OK" : "FAIL");

    if (kb_ok)    ps2_send_cmd(PS2_CMD_ENABLE_PORT1);
    if (mouse_ok) ps2_send_cmd(PS2_CMD_ENABLE_PORT2);

    if (kb_ok) {
        ps2_send_data(0xFF);
        for (int i = 0; i < 200000; i++) {
            if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)
                if (inb(PS2_DATA_PORT) == 0xAA) break;
            io_wait();
        }
        ps2_flush_output();
        ps2_send_data(0xF0); ps2_flush_output();
        ps2_send_data(0x01); ps2_flush_output();
        ps2_send_data(0xF4); ps2_flush_output();
        serial_writestring("[PS2] Keyboard ready (scancode set 1)\n");
    }

    if (mouse_ok) {
        ps2_mouse_cmd(0xFF);
        for (int i = 0; i < 200000; i++) {
            if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) inb(PS2_DATA_PORT);
            io_wait();
        }
        ps2_flush_output();
        ps2_mouse_cmd(0xF6);

        bool is_abs = ps2_detect_absolute();
        if (is_abs) {
            mouse_mode = MOUSE_MODE_ABSOLUTE;
            mouse_has_scroll = false;
            serial_writestring("[PS2] Mouse mode: ABSOLUTE (QEMU tablet)\n");
        } else {
            mouse_mode = MOUSE_MODE_RELATIVE;
            mouse_has_scroll = ps2_mouse_init_scroll();
            serial_printf("[PS2] Mouse mode: RELATIVE, scroll=%s\n",
                          mouse_has_scroll ? "YES" : "NO");
        }
        ps2_mouse_cmd_param(0xF3, 100);
        ps2_mouse_cmd(0xF4);
        serial_writestring("[PS2] Mouse: data reporting enabled\n");
    }

    ps2_send_cmd(PS2_CMD_READ_CONFIG);
    cfg = ps2_recv_data();
    if (kb_ok)    cfg |= PS2_CFG_PORT1_IRQ;
    if (mouse_ok) cfg |= PS2_CFG_PORT2_IRQ;
    ps2_send_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(cfg);

    if (kb_ok) {
        apic_setup_irq(KB_IRQ_LINE, KB_IRQ_VECTOR, false, 0);
        serial_printf("[PS2] Keyboard  IRQ%d -> vector 0x%02x\n", KB_IRQ_LINE, KB_IRQ_VECTOR);
    }

    if (mouse_ok) {
        apic_setup_irq(MOUSE_IRQ_LINE, MOUSE_IRQ_VECTOR, false, 0);
        serial_printf("[PS2] Mouse     IRQ%d -> vector 0x%02x\n", MOUSE_IRQ_LINE, MOUSE_IRQ_VECTOR);
    }

    kb_state  = (kb_state_t){0};
    kb_buf    = (kb_buf_t){0};

    mouse_state      = (mouse_state_t){0};
    mouse_packet_idx = 0;
    mouse_lost_sync  = 0;

    if (global_framebuffer) {
        mouse_screen_w = (int32_t)global_framebuffer->width;
        mouse_screen_h = (int32_t)global_framebuffer->height;
        mouse_state.x  = mouse_screen_w / 2;
        mouse_state.y  = mouse_screen_h / 2;
    }

    serial_printf("[PS2] Screen: %dx%d, cursor start: %d,%d\n",
                  (int)mouse_screen_w, (int)mouse_screen_h,
                  (int)mouse_state.x,  (int)mouse_state.y);
    serial_writestring("[PS2] Driver ready.\n");
    return true;
}

const kb_state_t*    ps2_kb_get_state(void)    {
    return (const kb_state_t*)&kb_state;
}
const mouse_state_t* ps2_mouse_get_state(void) {
    return (const mouse_state_t*)&mouse_state;
}

bool kb_buf_empty(void) {
    return kb_buf.head == kb_buf.tail;
}

bool kb_buf_try_getc(char *out) {
    if (kb_buf_empty()) return false;
    *out = kb_buf.buf[kb_buf.head];
    kb_buf.head = (kb_buf.head + 1) % KB_BUF_SIZE;
    return true;
}

bool kb_buf_has_ctrlc(void) {
    uint8_t h = kb_buf.head;
    while (h != kb_buf.tail) {
        if (kb_buf.buf[h] == 0x03) return true;
        h = (h + 1) % KB_BUF_SIZE;
    }
    return false;
}

void kb_buf_consume_ctrlc(void) {
    uint8_t h = kb_buf.head, t = kb_buf.tail;
    uint8_t pos = h;
    bool found = false;
    while (pos != t) {
        if (kb_buf.buf[pos] == 0x03) { found = true; break; }
        pos = (pos + 1) % KB_BUF_SIZE;
    }
    if (!found) return;
    uint8_t cur = pos;
    uint8_t nxt = (cur + 1) % KB_BUF_SIZE;
    while (nxt != t) {
        kb_buf.buf[cur] = kb_buf.buf[nxt];
        cur = nxt;
        nxt = (nxt + 1) % KB_BUF_SIZE;
    }
    kb_buf.tail = cur;
}

char kb_buf_getc(void) {
    char c;
    while (!kb_buf_try_getc(&c)) {
         //wait
    }
    return c;
}