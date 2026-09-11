bits 64
section .text

global _start
extern ld_start_c

_start:
    mov rdi, rsp
    mov rbx, rsp
    and rsp, -16
    call ld_start_c
    mov rsp, rbx
    xor rdx, rdx
    jmp rax
