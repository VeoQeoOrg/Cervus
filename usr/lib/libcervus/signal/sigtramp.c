#include <sys/syscall.h>

_Static_assert(SYS_RT_SIGRETURN == 603, "sigtramp: SYS_RT_SIGRETURN changed, update the asm literal");

void __sigtramp(void);

__asm__(
    ".text\n"
    ".globl __sigtramp\n"
    ".type __sigtramp,@function\n"
    "__sigtramp:\n"
    "    movq 8(%rsp), %rdi\n"
    "    movq (%rsp), %rax\n"
    "    call *%rax\n"
    "    addq $16, %rsp\n"
    "    movl $603, %eax\n"
    "    syscall\n"
    "    ud2\n"
    ".size __sigtramp, .-__sigtramp\n"
);
