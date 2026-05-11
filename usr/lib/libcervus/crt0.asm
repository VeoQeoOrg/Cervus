BITS 64
DEFAULT REL

section .text
    global _start
    extern main
    extern __cervus_argc
    extern __cervus_argv

_start:
    xor     rbp, rbp

    mov     rdi, [rsp]
    lea     rsi, [rsp + 8]

    lea     rax, [rel __cervus_argc]
    mov     dword [rax], edi
    lea     rax, [rel __cervus_argv]
    mov     qword [rax], rsi

    sub     rsp, 8 + 8*130
    mov     r10, rsp
    add     r10, 8

    xor     ecx, ecx
    mov     edx, edi
    xor     r8d, r8d

.loop:
    cmp     edx, ecx
    je      .done
    cmp     ecx, 128
    jge     .done

    mov     rax, [rsi + rcx*8]
    test    ecx, ecx
    jz      .keep
    test    rax, rax
    jz      .skip
    cmp     byte [rax], '-'
    jne     .keep
    cmp     byte [rax+1], '-'
    jne     .keep
    cmp     byte [rax+2], 'c'
    jne     .check_env
    cmp     byte [rax+3], 'w'
    jne     .keep
    cmp     byte [rax+4], 'd'
    jne     .keep
    cmp     byte [rax+5], '='
    jne     .keep
    jmp     .skip

.check_env:
    cmp     byte [rax+2], 'e'
    jne     .keep
    cmp     byte [rax+3], 'n'
    jne     .keep
    cmp     byte [rax+4], 'v'
    jne     .keep
    cmp     byte [rax+5], ':'
    jne     .keep
    jmp     .skip

.keep:
    mov     [r10 + r8*8], rax
    inc     r8d
.skip:
    inc     ecx
    jmp     .loop

.done:
    mov     qword [r10 + r8*8], 0

    and     rsp, -16

    mov     edi, r8d
    mov     rsi, r10
    call    main

    movsxd  rdi, eax
    xor     eax, eax
    syscall

.hang:
    hlt
    jmp     .hang

section .note.GNU-stack noalloc noexec nowrite progbits