section .text

extern task_exit

global context_switch
global first_task_start
global fpu_save
global fpu_restore
global task_trampoline

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

TASK_ENTRY_OFFSET equ 120
TASK_ARG_OFFSET   equ 128

task_trampoline:
    mov  rdi, [rbp + TASK_ARG_OFFSET]
    mov  rax, [rbp + TASK_ENTRY_OFFSET]

    xor  rbp, rbp

    call rax

    call task_exit
.hang:
    cli
    hlt
    jmp .hang