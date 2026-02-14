section .text
global fpu_save
global fpu_restore

fpu_save:
    fxsave [rdi]
    ret

fpu_restore:
    fxrstor [rdi]
    ret