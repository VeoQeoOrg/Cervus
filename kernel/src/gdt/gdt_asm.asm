section .text
global _reload_segments
global _load_gdt
global load_tss

_load_gdt:
    lgdt [rdi]
    ret

load_tss:
    ltr di
    ret

_reload_segments:
    push rdi

    lea rax, [rel .reload_cs]
    push rax

    retfq
.reload_cs:
    mov ax, si      ; ds
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret