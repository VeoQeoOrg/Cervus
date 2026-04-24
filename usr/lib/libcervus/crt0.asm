BITS 64
DEFAULT REL

section .bss
    global __cervus_argc
    global __cervus_argv
__cervus_argc:  resq 1
__cervus_argv:  resq 1

section .text
    global _start
    extern main

_start:
    mov     rdi, [rsp]
    lea     rsi, [rsp + 8]

    mov     [__cervus_argc], rdi
    mov     [__cervus_argv], rsi

    and     rsp, -16

    call    main

    mov     rdi, rax
    xor     rax, rax
    syscall

.hang:
    hlt
    jmp     .hang

section .note.GNU-stack noalloc noexec nowrite progbits
