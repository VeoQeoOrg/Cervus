%define MB2_MAGIC 0xE85250D6
%define KVIRT     0xffffffff80000000
%define KPHYS     0x200000
%define PHYS(x)   ((x) - KVIRT + KPHYS)

section .mb_header
align 8
hdr_start:
    dd MB2_MAGIC
    dd 0
    dd hdr_end - hdr_start
    dd 0x100000000 - (MB2_MAGIC + 0 + (hdr_end - hdr_start))
align 8
fb_tag:
    dw 5
    dw 1
    dd 20
    dd 1280
    dd 1024
    dd 32
align 8
reloc_tag:
    dw 10
    dw 0
    dd 24
    dd KPHYS
    dd 0x3FE00000
    dd 0x200000
    dd 1
align 8
entry_tag:
    dw 3
    dw 0
    dd 12
    dd PHYS(_mb_start)
align 8
end_tag:
    dw 0
    dw 0
    dd 8
hdr_end:

section .boot.data
align 8
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF
gdt64_ptr:
    dw gdt64_ptr - gdt64 - 1
    dd PHYS(gdt64)

section .boot.bss nobits align=4096
global mb_pml4
mb_pml4:      resb 4096
mb_pdpt_id:   resb 4096
mb_pdpt_hh:   resb 4096
mb_pdpt_k:    resb 4096
mb_pd_id:     resb 4096*16
mb_pd_k:      resb 4096
mb_stack:     resb 32768
mb_stack_top:
mb_magic:     resd 1
mb_info:      resd 1
global mb_load_delta
alignb 8
mb_load_delta: resq 1

section .boot.text
BITS 32
global _mb_start
extern mb2_main
extern mb_boot_stack

%macro SERIAL 1
    mov dx, 0x3F8
    mov al, %1
    out dx, al
%endmacro

_mb_start:
    cli
    cld
    xor ebp, ebp
    cmp eax, 0x36d76289
    jne .delta_done
    lea esi, [ebx + 8]
.tag_walk:
    mov ecx, [esi]
    test ecx, ecx
    jz .delta_done
    cmp ecx, 21
    je .tag_load_base
    mov ecx, [esi + 4]
    add ecx, 7
    and ecx, ~7
    add esi, ecx
    jmp .tag_walk
.tag_load_base:
    mov ebp, [esi + 8]
    sub ebp, KPHYS
.delta_done:
    lea esp, [ebp + PHYS(mb_stack_top)]
    mov [ebp + PHYS(mb_magic)], eax
    mov [ebp + PHYS(mb_info)], ebx
    mov [ebp + PHYS(mb_load_delta)], ebp
    mov dword [ebp + PHYS(mb_load_delta) + 4], 0
    SERIAL '1'

    xor eax, eax
    lea edi, [ebp + PHYS(mb_pml4)]
    mov ecx, (4096*4)/4
    rep stosd
    lea edi, [ebp + PHYS(mb_pdpt_id)]
    mov ecx, (4096*3)/4
    rep stosd

    lea edi, [ebp + PHYS(mb_pd_id)]
    mov eax, 0x83
    xor ebx, ebx
    mov ecx, 512*16
.fill_id:
    mov [edi], eax
    mov [edi+4], ebx
    add eax, 0x200000
    adc ebx, 0
    add edi, 8
    dec ecx
    jnz .fill_id

    lea edi, [ebp + PHYS(mb_pd_k)]
    lea eax, [ebp + KPHYS + 0x83]
    mov ecx, 512
.fill_k:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .fill_k

    lea eax, [ebp + PHYS(mb_pd_id) + 0*0x1000 + 0x03]
    mov [ebp + PHYS(mb_pdpt_id) + 0], eax
    lea eax, [ebp + PHYS(mb_pd_id) + 1*0x1000 + 0x03]
    mov [ebp + PHYS(mb_pdpt_id) + 8], eax
    lea eax, [ebp + PHYS(mb_pd_id) + 2*0x1000 + 0x03]
    mov [ebp + PHYS(mb_pdpt_id) + 16], eax
    lea eax, [ebp + PHYS(mb_pd_id) + 3*0x1000 + 0x03]
    mov [ebp + PHYS(mb_pdpt_id) + 24], eax

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 26
    jz .hh_2mb

    lea edi, [ebp + PHYS(mb_pdpt_hh)]
    mov eax, 0x83
    xor ebx, ebx
    mov ecx, 512
.hh_1g:
    mov [edi], eax
    mov [edi+4], ebx
    add eax, 0x40000000
    adc ebx, 0
    add edi, 8
    dec ecx
    jnz .hh_1g
    jmp .hh_done

.hh_2mb:
    lea edi, [ebp + PHYS(mb_pdpt_hh)]
    lea eax, [ebp + PHYS(mb_pd_id) + 0x03]
    mov ecx, 16
.hh_2mb_loop:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x1000
    add edi, 8
    dec ecx
    jnz .hh_2mb_loop
.hh_done:

    lea eax, [ebp + PHYS(mb_pd_k) + 0x03]
    mov [ebp + PHYS(mb_pdpt_k) + 510*8], eax

    lea eax, [ebp + PHYS(mb_pdpt_id) + 0x03]
    mov [ebp + PHYS(mb_pml4) + 0*8], eax
    lea eax, [ebp + PHYS(mb_pdpt_hh) + 0x03]
    mov [ebp + PHYS(mb_pml4) + 256*8], eax
    lea eax, [ebp + PHYS(mb_pdpt_k) + 0x03]
    mov [ebp + PHYS(mb_pml4) + 511*8], eax

    lea eax, [ebp + PHYS(mb_pml4)]
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    SERIAL '2'

    lea eax, [ebp + PHYS(gdt64)]
    mov [ebp + PHYS(gdt64_ptr) + 2], eax
    lgdt [ebp + PHYS(gdt64_ptr)]
    lea eax, [ebp + PHYS(long_entry)]
    push dword 0x08
    push eax
    retf

BITS 64
long_entry:
    mov ebp, ebp
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov dx, 0x3F8
    mov al, '3'
    out dx, al

    xor edi, edi
    mov edi, [rel mb_magic]
    xor esi, esi
    mov esi, [rel mb_info]

    mov rax, mb_boot_stack
    add rax, 65536
    mov rsp, rax

    mov rax, mb2_main
    call rax
.hang:
    cli
    hlt
    jmp .hang
