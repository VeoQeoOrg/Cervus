%define TRAMP 0x8000

section .rodata
align 16
global ap_trampoline_start
global ap_trampoline_end
global ap_tramp_cr3
global ap_tramp_entry
global ap_tramp_stack

BITS 16
ap_trampoline_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    lgdt [TRAMP + (tramp_gdt32_ptr - ap_trampoline_start)]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp dword 0x08:(TRAMP + (ap_pmode - ap_trampoline_start))

BITS 32
ap_pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax
    mov eax, [TRAMP + (ap_tramp_cr3 - ap_trampoline_start)]
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8) | (1 << 11)
    wrmsr
    mov eax, cr0
    or  eax, 0x80000001
    mov cr0, eax
    lgdt [TRAMP + (tramp_gdt64_ptr - ap_trampoline_start)]
    jmp 0x08:(TRAMP + (ap_lmode - ap_trampoline_start))

BITS 64
ap_lmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    or  rax, (1 << 1)
    mov cr0, rax
    mov rax, cr4
    or  rax, (1 << 9) | (1 << 10)
    mov cr4, rax
    mov eax, TRAMP + (ap_tramp_stack - ap_trampoline_start)
    mov rsp, [rax]
    xor edi, edi
    mov eax, TRAMP + (ap_tramp_entry - ap_trampoline_start)
    mov rax, [rax]
    call rax
.hang:
    cli
    hlt
    jmp .hang

align 16
tramp_gdt32:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
tramp_gdt32_ptr:
    dw (tramp_gdt32_ptr - tramp_gdt32) - 1
    dd TRAMP + (tramp_gdt32 - ap_trampoline_start)

align 16
tramp_gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
tramp_gdt64_ptr:
    dw (tramp_gdt64_ptr - tramp_gdt64) - 1
    dd TRAMP + (tramp_gdt64 - ap_trampoline_start)

align 8
ap_tramp_cr3:   dq 0
ap_tramp_entry: dq 0
ap_tramp_stack: dq 0
ap_trampoline_end:
