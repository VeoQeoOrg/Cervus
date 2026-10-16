section .text
global svm_enter

REG_RCX equ 8
REG_RDX equ 16
REG_RBX equ 24
REG_RBP equ 40
REG_RSI equ 48
REG_RDI equ 56
REG_R8  equ 64
REG_R9  equ 72
REG_R10 equ 80
REG_R11 equ 88
REG_R12 equ 96
REG_R13 equ 104
REG_R14 equ 112
REG_R15 equ 120

svm_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rdi
    push rdx
    push rsi

    mov rax, rdx
    vmsave
    mov rax, rsi
    vmload

    mov rcx, [rdi + REG_RCX]
    mov rdx, [rdi + REG_RDX]
    mov rbx, [rdi + REG_RBX]
    mov rbp, [rdi + REG_RBP]
    mov rsi, [rdi + REG_RSI]
    mov r8,  [rdi + REG_R8]
    mov r9,  [rdi + REG_R9]
    mov r10, [rdi + REG_R10]
    mov r11, [rdi + REG_R11]
    mov r12, [rdi + REG_R12]
    mov r13, [rdi + REG_R13]
    mov r14, [rdi + REG_R14]
    mov r15, [rdi + REG_R15]
    mov rdi, [rdi + REG_RDI]

    vmrun

    push rdi
    mov rdi, [rsp + 24]
    mov [rdi + REG_RCX], rcx
    mov [rdi + REG_RDX], rdx
    mov [rdi + REG_RBX], rbx
    mov [rdi + REG_RBP], rbp
    mov [rdi + REG_RSI], rsi
    mov [rdi + REG_R8],  r8
    mov [rdi + REG_R9],  r9
    mov [rdi + REG_R10], r10
    mov [rdi + REG_R11], r11
    mov [rdi + REG_R12], r12
    mov [rdi + REG_R13], r13
    mov [rdi + REG_R14], r14
    mov [rdi + REG_R15], r15
    pop rax
    mov [rdi + REG_RDI], rax

    pop rax
    vmsave
    pop rax
    vmload

    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
