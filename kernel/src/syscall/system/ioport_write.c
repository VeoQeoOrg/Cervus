#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/sched/capabilities.h"

int64_t sys_ioport_write(uint64_t port, uint64_t width, uint64_t val)
{
    task_t *t = syscall_cur_task();
    if (!t || !cap_has(t->capabilities, CAP_IOPORT)) return -EPERM;
    if (port > 0xFFFF) return -EINVAL;
    switch (width) {
        case 1: asm volatile("outb %b0,%w1" :: "a"((uint8_t)val),  "Nd"((uint16_t)port)); break;
        case 2: asm volatile("outw %w0,%w1" :: "a"((uint16_t)val), "Nd"((uint16_t)port)); break;
        case 4: asm volatile("outl %k0,%w1" :: "a"((uint32_t)val), "Nd"((uint16_t)port)); break;
        default: return -EINVAL;
    }
    return 0;
}
