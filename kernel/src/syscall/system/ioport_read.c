#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_ioport_read(uint64_t port, uint64_t width)
{
    task_t *t = syscall_cur_task();
    if (!t || !cap_has(t->capabilities, CAP_IOPORT)) return -EPERM;
    if (port > 0xFFFF) return -EINVAL;
    uint64_t v = 0;
    switch (width) {
        case 1: { uint8_t  x; asm volatile("inb %w1,%b0" : "=a"(x) : "Nd"((uint16_t)port)); v = x; break; }
        case 2: { uint16_t x; asm volatile("inw %w1,%w0" : "=a"(x) : "Nd"((uint16_t)port)); v = x; break; }
        case 4: { uint32_t x; asm volatile("inl %w1,%k0" : "=a"(x) : "Nd"((uint16_t)port)); v = x; break; }
        default: return -EINVAL;
    }
    return (int64_t)v;
}
