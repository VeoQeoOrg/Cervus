section .text
global __cervus_thread_trampoline
extern __cervus_thread_entry

__cervus_thread_trampoline:
    pop  rdi
    xor  rbp, rbp
    and  rsp, -16
    call __cervus_thread_entry wrt ..plt
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
