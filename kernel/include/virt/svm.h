#ifndef VIRT_SVM_H
#define VIRT_SVM_H

#include <stdint.h>
#include <stddef.h>

#define MSR_VM_CR       0xC0010114u
#define MSR_VM_HSAVE_PA 0xC0010117u

#define VM_CR_SVMDIS    (1ull << 4)
#define EFER_SVME       (1ull << 12)

#define SVM_FEAT_NP             (1u << 0)
#define SVM_FEAT_NRIPS          (1u << 3)
#define SVM_FEAT_FLUSH_BY_ASID  (1u << 6)
#define SVM_FEAT_DECODE_ASSISTS (1u << 7)

#define SVM_INTERCEPT_INTR      (1u << 0)
#define SVM_INTERCEPT_NMI       (1u << 1)
#define SVM_INTERCEPT_SMI       (1u << 2)
#define SVM_INTERCEPT_INIT      (1u << 3)
#define SVM_INTERCEPT_VINTR     (1u << 4)
#define SVM_INTERCEPT_RDPMC     (1u << 15)
#define SVM_INTERCEPT_CPUID     (1u << 18)
#define SVM_INTERCEPT_IRET      (1u << 20)
#define SVM_INTERCEPT_INVD      (1u << 22)
#define SVM_INTERCEPT_HLT       (1u << 24)
#define SVM_INTERCEPT_INVLPGA   (1u << 26)
#define SVM_INTERCEPT_IOIO      (1u << 27)
#define SVM_INTERCEPT_MSR       (1u << 28)
#define SVM_INTERCEPT_SHUTDOWN  (1u << 31)

#define SVM_INTERCEPT_VMRUN     (1u << 0)
#define SVM_INTERCEPT_VMMCALL   (1u << 1)
#define SVM_INTERCEPT_VMLOAD    (1u << 2)
#define SVM_INTERCEPT_VMSAVE    (1u << 3)
#define SVM_INTERCEPT_STGI      (1u << 4)
#define SVM_INTERCEPT_CLGI      (1u << 5)
#define SVM_INTERCEPT_SKINIT    (1u << 6)
#define SVM_INTERCEPT_MONITOR   (1u << 10)
#define SVM_INTERCEPT_MWAIT     (1u << 11)
#define SVM_INTERCEPT_MWAIT_CND (1u << 12)
#define SVM_INTERCEPT_XSETBV    (1u << 13)

#define SVM_TLB_NONE      0
#define SVM_TLB_FLUSH_ALL 1
#define SVM_TLB_FLUSH_ASID 3

#define SVM_V_TPR_MASK      0xFFu
#define SVM_V_IRQ           (1u << 8)
#define SVM_V_INTR_PRIO_SHIFT 16
#define SVM_V_IGN_TPR       (1u << 20)
#define SVM_V_INTR_MASKING  (1u << 24)

#define SVM_INT_SHADOW      (1u << 0)

#define SVM_NP_ENABLE       (1ull << 0)

#define SVM_EVTINJ_VEC_MASK 0xFFu
#define SVM_EVTINJ_TYPE_INTR  (0u << 8)
#define SVM_EVTINJ_TYPE_NMI   (2u << 8)
#define SVM_EVTINJ_TYPE_EXEPT (3u << 8)
#define SVM_EVTINJ_TYPE_SOFT  (4u << 8)
#define SVM_EVTINJ_TYPE_MASK  (7u << 8)
#define SVM_EVTINJ_VALID_ERR  (1u << 11)
#define SVM_EVTINJ_VALID      (1u << 31)

#define SVM_IOIO_IN        (1u << 0)
#define SVM_IOIO_STR       (1u << 2)
#define SVM_IOIO_REP       (1u << 3)
#define SVM_IOIO_SIZE_SHIFT 4
#define SVM_IOIO_ASIZE_SHIFT 7
#define SVM_IOIO_SEG_SHIFT 10

#define SVM_EXIT_READ_CR0   0x000
#define SVM_EXIT_WRITE_CR0  0x010
#define SVM_EXIT_READ_DR0   0x020
#define SVM_EXIT_WRITE_DR0  0x030
#define SVM_EXIT_EXCP_BASE  0x040
#define SVM_EXIT_INTR       0x060
#define SVM_EXIT_NMI        0x061
#define SVM_EXIT_SMI        0x062
#define SVM_EXIT_INIT       0x063
#define SVM_EXIT_VINTR      0x064
#define SVM_EXIT_RDPMC      0x06F
#define SVM_EXIT_CPUID      0x072
#define SVM_EXIT_IRET       0x074
#define SVM_EXIT_INVD       0x076
#define SVM_EXIT_PAUSE      0x077
#define SVM_EXIT_HLT        0x078
#define SVM_EXIT_INVLPGA    0x07A
#define SVM_EXIT_IOIO       0x07B
#define SVM_EXIT_MSR        0x07C
#define SVM_EXIT_SHUTDOWN   0x07F
#define SVM_EXIT_VMRUN      0x080
#define SVM_EXIT_VMMCALL    0x081
#define SVM_EXIT_VMLOAD     0x082
#define SVM_EXIT_VMSAVE     0x083
#define SVM_EXIT_STGI       0x084
#define SVM_EXIT_CLGI       0x085
#define SVM_EXIT_SKINIT     0x086
#define SVM_EXIT_MONITOR    0x08A
#define SVM_EXIT_MWAIT      0x08B
#define SVM_EXIT_MWAIT_COND 0x08C
#define SVM_EXIT_XSETBV     0x08D
#define SVM_EXIT_NPF        0x400
#define SVM_EXIT_ERR        0xFFFFFFFFu

typedef struct __attribute__((packed)) {
    uint16_t selector;
    uint16_t attrib;
    uint32_t limit;
    uint64_t base;
} vmcb_seg_t;

typedef struct __attribute__((packed)) {
    uint32_t intercept_cr;
    uint32_t intercept_dr;
    uint32_t intercept_exceptions;
    uint32_t intercept_misc1;
    uint32_t intercept_misc2;
    uint32_t intercept_misc3;
    uint8_t  reserved_0x18[0x3C - 0x18];
    uint16_t pause_filter_thresh;
    uint16_t pause_filter_count;
    uint64_t iopm_base_pa;
    uint64_t msrpm_base_pa;
    uint64_t tsc_offset;
    uint32_t asid;
    uint8_t  tlb_ctl;
    uint8_t  reserved_0x5d[3];
    uint32_t int_ctl;
    uint32_t int_vector;
    uint32_t int_state;
    uint8_t  reserved_0x6c[4];
    uint32_t exit_code;
    uint32_t exit_code_hi;
    uint64_t exit_info_1;
    uint64_t exit_info_2;
    uint32_t exit_int_info;
    uint32_t exit_int_info_err;
    uint64_t nested_ctl;
    uint64_t avic_vapic_bar;
    uint64_t ghcb_gpa;
    uint32_t event_inj;
    uint32_t event_inj_err;
    uint64_t nested_cr3;
    uint64_t virt_ext;
    uint32_t clean;
    uint32_t reserved_0xc4;
    uint64_t next_rip;
    uint8_t  insn_len;
    uint8_t  insn_bytes[15];
    uint8_t  reserved_0xe0[0x400 - 0xE0];
} vmcb_control_t;

typedef struct __attribute__((packed)) {
    vmcb_seg_t es;
    vmcb_seg_t cs;
    vmcb_seg_t ss;
    vmcb_seg_t ds;
    vmcb_seg_t fs;
    vmcb_seg_t gs;
    vmcb_seg_t gdtr;
    vmcb_seg_t ldtr;
    vmcb_seg_t idtr;
    vmcb_seg_t tr;
    uint8_t  reserved_0xa0[42];
    uint8_t  vmpl;
    uint8_t  cpl;
    uint8_t  reserved_0xcc[4];
    uint64_t efer;
    uint8_t  reserved_0xd8[112];
    uint64_t cr4;
    uint64_t cr3;
    uint64_t cr0;
    uint64_t dr7;
    uint64_t dr6;
    uint64_t rflags;
    uint64_t rip;
    uint8_t  reserved_0x180[88];
    uint64_t rsp;
    uint64_t s_cet;
    uint64_t ssp;
    uint64_t isst_addr;
    uint64_t rax;
    uint64_t star;
    uint64_t lstar;
    uint64_t cstar;
    uint64_t sfmask;
    uint64_t kernel_gs_base;
    uint64_t sysenter_cs;
    uint64_t sysenter_esp;
    uint64_t sysenter_eip;
    uint64_t cr2;
    uint8_t  reserved_0x248[32];
    uint64_t g_pat;
    uint64_t dbgctl;
    uint64_t br_from;
    uint64_t br_to;
    uint64_t last_excp_from;
    uint64_t last_excp_to;
} vmcb_save_t;

typedef struct __attribute__((packed)) {
    vmcb_control_t control;
    vmcb_save_t    save;
} vmcb_t;

_Static_assert(offsetof(vmcb_control_t, iopm_base_pa) == 0x040, "vmcb iopm");
_Static_assert(offsetof(vmcb_control_t, asid) == 0x058, "vmcb asid");
_Static_assert(offsetof(vmcb_control_t, int_ctl) == 0x060, "vmcb int_ctl");
_Static_assert(offsetof(vmcb_control_t, exit_code) == 0x070, "vmcb exit_code");
_Static_assert(offsetof(vmcb_control_t, exit_int_info) == 0x088, "vmcb exit_int_info");
_Static_assert(offsetof(vmcb_control_t, nested_ctl) == 0x090, "vmcb nested_ctl");
_Static_assert(offsetof(vmcb_control_t, event_inj) == 0x0A8, "vmcb event_inj");
_Static_assert(offsetof(vmcb_control_t, nested_cr3) == 0x0B0, "vmcb nested_cr3");
_Static_assert(offsetof(vmcb_control_t, next_rip) == 0x0C8, "vmcb next_rip");
_Static_assert(sizeof(vmcb_control_t) == 0x400, "vmcb control size");
_Static_assert(offsetof(vmcb_save_t, cpl) == 0x0CB, "vmcb cpl");
_Static_assert(offsetof(vmcb_save_t, efer) == 0x0D0, "vmcb efer");
_Static_assert(offsetof(vmcb_save_t, cr4) == 0x148, "vmcb cr4");
_Static_assert(offsetof(vmcb_save_t, rip) == 0x178, "vmcb rip");
_Static_assert(offsetof(vmcb_save_t, rsp) == 0x1D8, "vmcb rsp");
_Static_assert(offsetof(vmcb_save_t, rax) == 0x1F8, "vmcb rax");
_Static_assert(offsetof(vmcb_save_t, cr2) == 0x240, "vmcb cr2");
_Static_assert(offsetof(vmcb_save_t, g_pat) == 0x268, "vmcb g_pat");

static inline void svm_clgi(void) { asm volatile("clgi" ::: "memory"); }
static inline void svm_stgi(void) { asm volatile("stgi" ::: "memory"); }

#endif
