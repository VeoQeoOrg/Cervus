section .text
extern isr_handler

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rax, ds
    push rax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rdi, rsp
    call isr_handler

    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

%macro ISR_ERR_STUB 1
isr_stub_%1:
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_NO_ERR_STUB 1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%assign i 0
%rep 32
    %if i = 8 || i = 10 || i = 11 || i = 12 || i = 13 || i = 14 || i = 17 || i = 21 || i = 29 || i = 30
        ISR_ERR_STUB i
    %else
        ISR_NO_ERR_STUB i
    %endif
    %assign i i+1
%endrep

%rep 224
    ISR_NO_ERR_STUB i
    %assign i i+1
%endrep

section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
    %assign i i+1
%endrep