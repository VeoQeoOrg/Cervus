BITS 64
DEFAULT REL

section .text
    global _start
    extern main
    extern __cervus_argc
    extern __cervus_argv
    extern __cervus_filter_args
    extern __cervus_filtered_argv

_start:
    xor     rbp, rbp

    mov     rdi, [rsp]
    lea     rsi, [rsp + 8]

    lea     rax, [rel __cervus_argc]
    mov     dword [rax], edi
    lea     rax, [rel __cervus_argv]
    mov     qword [rax], rsi

    and     rsp, -16

    movsxd  rdi, dword [rel __cervus_argc]
    mov     rsi, qword [rel __cervus_argv]
    call    __cervus_filter_args

    movsxd  rdi, eax
    lea     rsi, [rel __cervus_filtered_argv]
    call    main

    movsxd  rdi, eax
    xor     rax, rax
    syscall

.hang:
    hlt
    jmp     .hang

section .note.GNU-stack noalloc noexec nowrite progbits