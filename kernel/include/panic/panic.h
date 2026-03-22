#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "../interrupts/interrupts.h"

__attribute__((noreturn))
void kernel_panic(const char *msg);

__attribute__((noreturn))
void kernel_panic_regs(const char *msg, struct int_frame_t *regs);

#define KPANIC(msg) \
    kernel_panic("[" __FILE__ ":" STRINGIFY(__LINE__) "] " msg)

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

#endif