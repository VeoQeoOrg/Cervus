#include <sys/syscall.h>
#include <signal.h>

_Static_assert(SYS_RT_SIGRETURN == 603, "sigtramp: SYS_RT_SIGRETURN changed, update the asm literal");
_Static_assert(sizeof(siginfo_t) == 128, "siginfo_t must match the Linux layout");

void __sigtramp(void);

__asm__(
    ".text\n"
    ".globl __sigtramp\n"
    ".type __sigtramp,@function\n"
    "__sigtramp:\n"
    "    movq 8(%rsp), %rdi\n"
    "    xorl %esi, %esi\n"
    "    xorl %edx, %edx\n"
    "    movabsq $0x313046535643, %rax\n"
    "    cmpq %rax, 104(%rsp)\n"
    "    jne 1f\n"
    "    testq $2, 112(%rsp)\n"
    "    jz 1f\n"
    "    movq 128(%rsp), %rsi\n"
    "    movq 120(%rsp), %rdx\n"
    "1:\n"
    "    movq (%rsp), %rax\n"
    "    call *%rax\n"
    "    addq $16, %rsp\n"
    "    movl $603, %eax\n"
    "    syscall\n"
    "    ud2\n"
    ".size __sigtramp, .-__sigtramp\n"
);
