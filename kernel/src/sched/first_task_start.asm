section .text
global first_task_start

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