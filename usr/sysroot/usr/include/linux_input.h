#ifndef _LINUX_INPUT_H
#define _LINUX_INPUT_H

#include <stdint.h>

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02

#define SYN_REPORT 0

#define REL_X 0
#define REL_Y 1

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

struct input_event {
    uint64_t sec;
    uint64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} __attribute__((packed));

#endif
