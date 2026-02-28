section .text

global syscall_entry
extern syscall_handler_c

syscall_entry:
    swapgs
    mov [gs:8], rsp
    mov rsp, [gs:0]

    push r11
    push rcx

    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp

    mov rcx, rdx
    mov r9,  r8
    mov r8,  r10
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    call syscall_handler_c

    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx

    pop rcx
    pop r11

    mov rsp, [gs:8]
    swapgs

    o64 sysret