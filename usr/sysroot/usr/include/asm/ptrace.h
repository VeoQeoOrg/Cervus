#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

struct pt_regs {
    unsigned long r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
    unsigned long rax, rcx, rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp, ss;
};

#endif
