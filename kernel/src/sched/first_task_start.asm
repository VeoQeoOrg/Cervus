; first_task_start.asm
; Функция для первого запуска задачи (никогда не возвращается!)

section .text
global first_task_start

; void first_task_start(task_t* task)
; rdi = указатель на task_t
; task->rsp находится на offset 0

first_task_start:
    ; Загружаем rsp из task->rsp (offset 0)
    mov rsp, [rdi]

    ; Обнуляем rbp
    xor rbp, rbp

    ; Восстанавливаем регистры из стека
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; Переходим к entry point задачи (адрес на вершине стека)
    ret