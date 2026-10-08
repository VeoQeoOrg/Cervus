BITS 64
DEFAULT REL

section .text

global setjmp
global longjmp
global _setjmp
global _longjmp
global sigsetjmp
global siglongjmp
extern __cervus_sigsetjmp_tail

setjmp:
_setjmp:
setjmp_body:
    mov     [rdi + 0x00], rbx
    mov     [rdi + 0x08], rbp
    mov     [rdi + 0x10], r12
    mov     [rdi + 0x18], r13
    mov     [rdi + 0x20], r14
    mov     [rdi + 0x28], r15
    lea     rax, [rsp + 8]
    mov     [rdi + 0x30], rax
    mov     rax, [rsp]
    mov     [rdi + 0x38], rax
    xor     eax, eax
    ret

longjmp:
_longjmp:
jmp_body:
    mov     rbx, [rdi + 0x00]
    mov     rbp, [rdi + 0x08]
    mov     r12, [rdi + 0x10]
    mov     r13, [rdi + 0x18]
    mov     r14, [rdi + 0x20]
    mov     r15, [rdi + 0x28]
    mov     rsp, [rdi + 0x30]
    mov     eax, esi
    test    eax, eax
    jnz     .done
    mov     eax, 1
.done:
    jmp     qword [rdi + 0x38]

sigsetjmp:
    test    esi, esi
    jz      setjmp_body
    pop     qword [rdi + 64]
    mov     [rdi + 72], rbx
    mov     rbx, rdi
    call    setjmp_body
    push    qword [rbx + 64]
    mov     rdi, rbx
    mov     esi, eax
    mov     rbx, [rbx + 72]
    jmp     __cervus_sigsetjmp_tail wrt ..plt

siglongjmp:
    jmp     jmp_body

section .note.GNU-stack noalloc noexec nowrite progbits