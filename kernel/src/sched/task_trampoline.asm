section .text
global task_trampoline

TASK_ENTRY_OFFSET equ 120
TASK_ARG_OFFSET   equ 128

task_trampoline:
    mov  rdi, [rbp + TASK_ARG_OFFSET]
    mov  rax, [rbp + TASK_ENTRY_OFFSET]
    xor  rbp, rbp
    call rax

    ; TODO: task_exit() — ZOMBIE
.hang:
    cli
    hlt
    jmp .hang