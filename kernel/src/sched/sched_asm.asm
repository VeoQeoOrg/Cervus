section .text
extern task_exit
global context_switch
global first_task_start
global fpu_save
global fpu_restore
global task_trampoline
global task_trampoline_user
global task_trampoline_fork

TASK_ENTRY_OFFSET            equ 120
TASK_ARG_OFFSET              equ 128
TASK_USER_RSP_OFFSET         equ 144
TASK_CR3_OFFSET              equ 24
TASK_USER_SAVED_RIP_OFFSET   equ 264
TASK_USER_SAVED_RBP_OFFSET   equ 272
TASK_USER_SAVED_RBX_OFFSET   equ 280
TASK_USER_SAVED_R12_OFFSET   equ 288
TASK_USER_SAVED_R13_OFFSET   equ 296
TASK_USER_SAVED_R14_OFFSET   equ 304
TASK_USER_SAVED_R15_OFFSET   equ 312
TASK_USER_SAVED_R11_OFFSET   equ 320

PERCPU_CURRENT_TASK equ 24
TASK_ON_CPU_OFFSET  equ 0x150

context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp

    mfence

    mov byte [rdi + TASK_ON_CPU_OFFSET], 0

    test rdx, rdx
    jz .skip_global
    mov [rdx], rsi
.skip_global:
    mov [gs:PERCPU_CURRENT_TASK], rsi

    mov rsp, [rsi]

    test rcx, rcx
    jz .skip_cr3
    mov cr3, rcx
.skip_cr3:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

first_task_start:
    mov [gs:PERCPU_CURRENT_TASK], rdi

    mov rsi, [rdi + TASK_CR3_OFFSET]
    mov rsp, [rdi]

    test rsi, rsi
    jz .skip_cr3_fts
    mov cr3, rsi
.skip_cr3_fts:

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

fpu_save:
    fxsave [rdi]
    ret

fpu_restore:
    fxrstor [rdi]
    ret

task_trampoline:
    mov  rdi, [rbp + TASK_ARG_OFFSET]
    mov  rax, [rbp + TASK_ENTRY_OFFSET]
    xor  rbp, rbp
    sti
    call rax
    call task_exit
.hang:
    cli
    hlt
    jmp .hang

task_trampoline_user:
    mov  rax, [rbp + TASK_ENTRY_OFFSET]
    mov  rcx, [rbp + TASK_USER_RSP_OFFSET]

    xor  rbp, rbp

    push qword 0x1B
    push rcx
    pushfq
    pop  rdx
    or   rdx, (1 << 9)
    and  rdx, ~(3 << 12)
    push rdx
    push qword 0x23
    push rax

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rdi, rdi
    xor rsi, rsi
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    swapgs
    iretq

task_trampoline_fork:
    cli

    mov rax, [rbp + TASK_USER_SAVED_RIP_OFFSET]
    mov rcx, [rbp + TASK_USER_RSP_OFFSET]
    mov r11, [rbp + TASK_USER_SAVED_R11_OFFSET]
    mov rbx, [rbp + TASK_USER_SAVED_RBX_OFFSET]
    mov r12, [rbp + TASK_USER_SAVED_R12_OFFSET]
    mov r13, [rbp + TASK_USER_SAVED_R13_OFFSET]
    mov r14, [rbp + TASK_USER_SAVED_R14_OFFSET]
    mov r15, [rbp + TASK_USER_SAVED_R15_OFFSET]
    mov rbp, [rbp + TASK_USER_SAVED_RBP_OFFSET]

    or r11, (1 << 9)

    push qword 0x1B
    push rcx
    push r11
    push qword 0x23
    push rax

    xor rax, rax

    swapgs
    iretq