%define MB2_MAGIC 0xE85250D6
%define KVIRT     0xffffffff80000000
%define KPHYS     0x200000

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
    dw 0
    dd 20
    dd 0
    dd 0
    dd 0
align 8
entry_tag:
    dw 3
    dw 0
    dd 12
    dd _mb_start
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
    dd gdt64

section .boot.bss nobits align=4096
global mb_pml4
mb_pml4:      resb 4096
mb_pdpt_id:   resb 4096
mb_pdpt_hh:   resb 4096
mb_pdpt_k:    resb 4096
mb_pd_id:     resb 4096*4
mb_pd_k:      resb 4096
mb_stack:     resb 32768
mb_stack_top:
mb_magic:     resd 1
mb_info:      resd 1

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
    mov esp, mb_stack_top
    mov [mb_magic], eax
    mov [mb_info], ebx
    SERIAL '1'

    xor eax, eax
    mov edi, mb_pml4
    mov ecx, (4096*4)/4
    rep stosd
    mov edi, mb_pdpt_id
    mov ecx, (4096*3)/4
    rep stosd

    mov edi, mb_pd_id
    mov eax, 0x83
    xor ebx, ebx
    mov ecx, 512*4
.fill_id:
    mov [edi], eax
    mov [edi+4], ebx
    add eax, 0x200000
    adc ebx, 0
    add edi, 8
    dec ecx
    jnz .fill_id

    mov edi, mb_pd_k
    mov eax, KPHYS | 0x83
    mov ecx, 512
.fill_k:
    mov [edi], eax
    mov dword [edi+4], 0
    add eax, 0x200000
    add edi, 8
    dec ecx
    jnz .fill_k

    mov dword [mb_pdpt_id + 0],  mb_pd_id + 0*0x1000 + 0x03
    mov dword [mb_pdpt_id + 8],  mb_pd_id + 1*0x1000 + 0x03
    mov dword [mb_pdpt_id + 16], mb_pd_id + 2*0x1000 + 0x03
    mov dword [mb_pdpt_id + 24], mb_pd_id + 3*0x1000 + 0x03
    mov dword [mb_pdpt_hh + 0],  mb_pd_id + 0*0x1000 + 0x03
    mov dword [mb_pdpt_hh + 8],  mb_pd_id + 1*0x1000 + 0x03
    mov dword [mb_pdpt_hh + 16], mb_pd_id + 2*0x1000 + 0x03
    mov dword [mb_pdpt_hh + 24], mb_pd_id + 3*0x1000 + 0x03
    mov dword [mb_pdpt_k + 510*8], mb_pd_k + 0x03

    mov dword [mb_pml4 + 0*8],   mb_pdpt_id + 0x03
    mov dword [mb_pml4 + 256*8], mb_pdpt_hh + 0x03
    mov dword [mb_pml4 + 511*8], mb_pdpt_k  + 0x03

    mov eax, mb_pml4
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

    lgdt [gdt64_ptr]
    jmp 0x08:long_entry

BITS 64
long_entry:
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
    mov edi, [mb_magic]
    xor esi, esi
    mov esi, [mb_info]

    mov rax, mb_boot_stack
    add rax, 65536
    mov rsp, rax

    mov rax, mb2_main
    call rax
.hang:
    cli
    hlt
    jmp .hang
