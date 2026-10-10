section .text
global vmx_enter
global vmx_exit_point

VMCS_HOST_RSP equ 0x6C14

REG_RAX equ 0
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
REG_CR2 equ 128

vmx_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rdi

    mov rax, VMCS_HOST_RSP
    vmwrite rax, rsp
    jbe .write_failed

    mov rax, [rdi + REG_CR2]
    mov rdx, cr2
    cmp rax, rdx
    je .cr2_done
    mov cr2, rax
.cr2_done:

    test esi, esi

    mov rax, [rdi + REG_RAX]
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

    jnz .resume
    vmlaunch
    jmp .entry_failed
.resume:
    vmresume

.entry_failed:
    mov eax, 1
    jc .fail_out
    mov eax, 2
.fail_out:
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

.write_failed:
    mov eax, 3
    jmp .fail_out

vmx_exit_point:
    push rdi
    mov rdi, [rsp + 8]
    mov [rdi + REG_RAX], rax
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
    mov rax, cr2
    mov [rdi + REG_CR2], rax
    pop rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    xor eax, eax
    ret
