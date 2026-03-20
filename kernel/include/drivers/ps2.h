#ifndef PS2_H
#define PS2_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PS2_DATA_PORT           0x60
#define PS2_STATUS_PORT         0x64
#define PS2_CMD_PORT            0x64

#define PS2_STATUS_OUTPUT_FULL  (1 << 0)
#define PS2_STATUS_INPUT_FULL   (1 << 1)

#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_SELF_TEST       0xAA
#define PS2_CMD_TEST_PORT1      0xAB
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_ENABLE_PORT1    0xAE
#define PS2_CMD_WRITE_PORT2     0xD4

#define PS2_CFG_PORT1_IRQ       (1 << 0)
#define PS2_CFG_PORT2_IRQ       (1 << 1)
#define PS2_CFG_PORT1_XLAT      (1 << 6)

#define PS2_KEY_RELEASE_BIT     0x80

#define SC_LSHIFT               0x2A
#define SC_RSHIFT               0x36
#define SC_CAPS                 0x3A
#define SC_LCTRL                0x1D
#define SC_LALT                 0x38
#define SC_ESCAPE               0x01
#define SC_F1                   0x3B
#define SC_F2                   0x3C
#define SC_F3                   0x3D
#define SC_F4                   0x3E
#define SC_F5                   0x3F
#define SC_F6                   0x40
#define SC_F7                   0x41
#define SC_F8                   0x42
#define SC_F9                   0x43
#define SC_F10                  0x44
#define SC_F11                  0x57
#define SC_F12                  0x58

#define MOUSE_BTN_LEFT          (1 << 0)
#define MOUSE_BTN_RIGHT         (1 << 1)
#define MOUSE_BTN_MIDDLE        (1 << 2)
#define MOUSE_X_SIGN            (1 << 4)
#define MOUSE_Y_SIGN            (1 << 5)
#define MOUSE_X_OVERFLOW        (1 << 6)
#define MOUSE_Y_OVERFLOW        (1 << 7)

typedef enum {
    MOUSE_SCROLL_NONE = 0,
    MOUSE_SCROLL_UP,
    MOUSE_SCROLL_DOWN,
} mouse_scroll_t;

typedef struct {
    int32_t        x, y;
    bool           btn_left, btn_right, btn_middle;
    mouse_scroll_t scroll;
} mouse_state_t;

typedef struct {
    bool shift, caps_lock, ctrl, alt;
} kb_state_t;

#define KB_BUF_SIZE 64

typedef struct {
    char    buf[KB_BUF_SIZE];
    uint8_t head, tail;
} kb_buf_t;

bool                  ps2_init(void);
const kb_state_t*     ps2_kb_get_state(void);
const mouse_state_t*  ps2_mouse_get_state(void);
bool  kb_buf_empty(void);
char  kb_buf_getc(void);
bool  kb_buf_try_getc(char *out);
bool  kb_buf_has_ctrlc(void);
void  kb_buf_consume_ctrlc(void);

#endif