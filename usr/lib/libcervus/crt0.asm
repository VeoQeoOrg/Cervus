BITS 64
DEFAULT REL

section .text
    global _start
    extern main
    extern exit
    extern __cervus_argc
    extern __cervus_argv
    extern environ
    extern __cervus_tls_init
    extern __cervus_run_init
    extern __preinit_array_start
    extern __preinit_array_end
    extern __init_array_start
    extern __init_array_end
    extern __fini_array_start
    extern __fini_array_end

_start:
    xor     rbp, rbp

    mov     r12, [rsp]
    lea     r13, [rsp + 8]
    lea     r14, [r13 + r12*8 + 8]

    mov     rax, [rel __cervus_argc wrt ..gotpc]
    mov     dword [rax], r12d
    mov     rax, [rel __cervus_argv wrt ..gotpc]
    mov     qword [rax], r13
    mov     rax, [rel environ wrt ..gotpc]
    mov     qword [rax], r14

    and     rsp, -16
    sub     rsp, 48
    lea     rax, [rel __preinit_array_start]
    mov     [rsp], rax
    lea     rax, [rel __preinit_array_end]
    mov     [rsp + 8], rax
    lea     rax, [rel __init_array_start]
    mov     [rsp + 16], rax
    lea     rax, [rel __init_array_end]
    mov     [rsp + 24], rax
    lea     rax, [rel __fini_array_start]
    mov     [rsp + 32], rax
    lea     rax, [rel __fini_array_end]
    mov     [rsp + 40], rax
    mov     r15, rsp

    call    __cervus_tls_init wrt ..plt

    mov     edi, r12d
    mov     rsi, r13
    mov     rdx, r14
    mov     rcx, r15
    call    __cervus_run_init wrt ..plt

    mov     edi, r12d
    mov     rsi, r13
    mov     rax, [rel environ wrt ..gotpc]
    mov     rdx, [rax]
    call    main wrt ..plt

    movsxd  rdi, eax
    call    exit wrt ..plt

.hang:
    hlt
    jmp     .hang

section .note.GNU-stack noalloc noexec nowrite progbits
