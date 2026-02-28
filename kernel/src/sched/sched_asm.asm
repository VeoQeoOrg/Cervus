section .text

extern task_exit

global context_switch
global first_task_start
global fpu_save
global fpu_restore
global task_trampoline
global task_trampoline_user

context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp

    mov rsp, [rsi]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

first_task_start:
    mov rsp, [rdi]

    xor rbp, rbp

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

TASK_ENTRY_OFFSET    equ 120
TASK_ARG_OFFSET      equ 128
TASK_USER_RSP_OFFSET equ 144

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

    iretq