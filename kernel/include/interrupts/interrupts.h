#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

struct int_frame_t {
    uint64_t ds;

    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;

    uint64_t rdi;
    uint64_t rsi;

    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t interrupt;
    uint64_t error;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

typedef void (*int_handler_f)(struct int_frame_t* frame);

typedef struct {
    uint64_t vector;
    int_handler_f handler;
} int_desc_t;

#define __CHECK_HANDLER(fn) \
    _Static_assert( \
        __builtin_types_compatible_p( \
            typeof(fn), void (*)(struct int_frame_t *)), \
        "Invalid interrupt handler signature")

#define __CONCAT(a, b) a##b
#define __UNIQUE_NAME(base) __CONCAT(base, __COUNTER__)

#define DEFINE_ISR(_vector, _name)                                  \
    static void _name(struct int_frame_t *frame);                  \
    static const int_desc_t __UNIQUE_NAME(__isr_desc_##_name)      \
    __attribute__((used, section(".isr_handlers"))) = {            \
        .vector  = (_vector),                                      \
        .handler = _name,                                          \
    };                                                             \
    static void _name(struct int_frame_t *frame)


#define DEFINE_IRQ(_vector, _name)                                  \
    static void _name(struct int_frame_t *frame);                  \
    static const int_desc_t __UNIQUE_NAME(__irq_desc_##_name)      \
    __attribute__((used, section(".irq_handlers"))) = {            \
        .vector  = (_vector),                                      \
        .handler = _name,                                          \
    };                                                             \
    static void _name(struct int_frame_t *frame)

void init_interrupt_system();

#endif
