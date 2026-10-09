#include <stdarg.h>
#include <stdint.h>
#include <ucontext.h>

extern void __cervus_ctx_start(void) __attribute__((visibility("hidden")));

void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
    greg_t *g = ucp->uc_mcontext.gregs;
    int nstack = argc > 6 ? argc - 6 : 0;
    uintptr_t sp = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
    sp -= (uintptr_t)nstack * 8;
    sp &= ~(uintptr_t)15;
    sp -= 8;
    uint64_t *frame = (uint64_t *)sp;
    frame[0] = (uint64_t)(uintptr_t)__cervus_ctx_start;

    static const int regs[6] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc; i++) {
        uint64_t v = va_arg(ap, uint64_t);
        if (i < 6) g[regs[i]] = v;
        else frame[1 + i - 6] = v;
    }
    va_end(ap);

    g[REG_RBX] = (greg_t)(uintptr_t)ucp->uc_link;
    g[REG_RSP] = (greg_t)sp;
    g[REG_RIP] = (greg_t)(uintptr_t)func;
    g[REG_RAX] = 0;
}
