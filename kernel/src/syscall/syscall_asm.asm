section .text
global syscall_entry
extern syscall_handler_c
extern sched_reschedule

PERCPU_KERNEL_RSP   equ 0
PERCPU_USER_RSP     equ 8
PERCPU_SAVED_RBP    equ 48
PERCPU_SAVED_RBX    equ 56
PERCPU_SAVED_R12    equ 64
PERCPU_SAVED_R13    equ 72
PERCPU_SAVED_R14    equ 80
PERCPU_SAVED_R15    equ 88
PERCPU_SAVED_R11    equ 96
PERCPU_SAVED_RIP    equ 104
PERCPU_NEED_RESCHED equ 40
PERCPU_CURRENT_TASK equ 24

TASK_USER_RSP       equ 144
TASK_USER_SAVED_RIP equ 272
TASK_USER_SAVED_RBP equ 280
TASK_USER_SAVED_RBX equ 288
TASK_USER_SAVED_R12 equ 296
TASK_USER_SAVED_R13 equ 304
TASK_USER_SAVED_R14 equ 312
TASK_USER_SAVED_R15 equ 320
TASK_USER_SAVED_R11 equ 328

syscall_entry:
    swapgs
    mov  [gs:PERCPU_USER_RSP], rsp
    mov  [gs:PERCPU_SAVED_RBP], rbp
    mov  [gs:PERCPU_SAVED_RBX], rbx
    mov  [gs:PERCPU_SAVED_R12], r12
    mov  [gs:PERCPU_SAVED_R13], r13
    mov  [gs:PERCPU_SAVED_R14], r14
    mov  [gs:PERCPU_SAVED_R15], r15
    mov  [gs:PERCPU_SAVED_R11], r11
    mov  [gs:PERCPU_SAVED_RIP], rcx
    mov  rsp, [gs:PERCPU_KERNEL_RSP]

    push r11
    push rcx
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp

    push rcx
    mov  rcx, rdx
    mov  r9,  r8
    mov  r8,  r10
    mov  rdx, rsi
    mov  rsi, rdi
    mov  rdi, rax
    call syscall_handler_c

    add  rsp, 8
    pop  rbp
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rcx
    pop  r11

.check_resched:
    cmp  byte [gs:PERCPU_NEED_RESCHED], 0
    je   .no_resched
    mov  byte [gs:PERCPU_NEED_RESCHED], 0
    push rax
    call sched_reschedule
    pop  rax
    jmp  .check_resched

.no_resched:
    cli

    mov  r10, rax
    mov  rax, [gs:PERCPU_CURRENT_TASK]

    mov  rcx, [rax + TASK_USER_SAVED_RIP]
    test rcx, rcx
    jnz  .rip_ok
    push rax
    push rcx
    extern serial_printf
    mov  rdi, rax
    mov  rsi, [rax + 168]
    mov  esi, esi
    mov  rdx, [rax + 272]
    mov  rcx, [rax + 144]
    lea  rdi, [rel .fmt_zero_rip]
    call serial_printf
    lea  rdi, [rel .msg_zero_rip]
    extern kernel_panic
    call kernel_panic
.fmt_zero_rip: db "[NO_RESCHED-BUG] task=0x%llx pid=%u user_saved_rip=0x%llx user_rsp=0x%llx", 10, 0
.msg_zero_rip: db "sysret: user_saved_rip=0 — would fault at NULL", 0
.rip_ok:
    mov  rsp, [rax + TASK_USER_RSP]
    mov  rcx, [rax + TASK_USER_SAVED_RIP]
    mov  r11, [rax + TASK_USER_SAVED_R11]
    mov  rbp, [rax + TASK_USER_SAVED_RBP]
    mov  rbx, [rax + TASK_USER_SAVED_RBX]
    mov  r12, [rax + TASK_USER_SAVED_R12]
    mov  r13, [rax + TASK_USER_SAVED_R13]
    mov  r14, [rax + TASK_USER_SAVED_R14]
    mov  r15, [rax + TASK_USER_SAVED_R15]
    mov  rax, r10

    and  r11, 0x00000000003C0FFF
    or   r11, 0x0000000000000202

    mov  r9, rcx
    shr  r9, 47
    jnz  .sysret_bad_rip

    mov  r9, rsp
    shr  r9, 47
    jnz  .sysret_bad_rsp

    swapgs
    o64 sysret

.sysret_bad_rip:
    extern sysret_bad_rip_panic
    sti
    mov  rdi, rcx
    mov  rsi, rax
    call sysret_bad_rip_panic
    cli
    hlt

.sysret_bad_rsp:
    extern sysret_bad_rsp_panic
    sti
    mov  rdi, rsp
    mov  rsi, rcx
    call sysret_bad_rsp_panic
    cli
    hlt