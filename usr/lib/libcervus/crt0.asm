BITS 64
DEFAULT REL

section .text
    global _start
    extern main
    extern exit
    extern __cervus_argc
    extern __cervus_argv
    extern environ

_start:
    xor     rbp, rbp

    mov     rdi, [rsp]
    lea     rsi, [rsp + 8]

    lea     rax, [rel __cervus_argc]
    mov     dword [rax], edi
    lea     rax, [rel __cervus_argv]
    mov     qword [rax], rsi

    mov     rdx, rsi
    mov     ecx, edi
    lea     rdx, [rdx + rcx*8 + 8]
    lea     rax, [rel environ]
    mov     qword [rax], rdx

    and     rsp, -16

    call    main

    movsxd  rdi, eax
    call    exit

.hang:
    hlt
    jmp     .hang

section .note.GNU-stack noalloc noexec nowrite progbits
