section .text
global context_switch

; void context_switch(task_t* old, task_t* new)
; rdi = old task
; rsi = new task

context_switch:
    ; Сохраняем регистры текущей задачи
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Сохраняем RSP в old->rsp (смещение 0 в структуре task_t)
    mov [rdi], rsp

    ; Загружаем RSP из new->rsp
    mov rsp, [rsi]

    ; Восстанавливаем регистры новой задачи
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret