#ifndef VIRT_VMX_H
#define VIRT_VMX_H

#include <stdint.h>
#include <stdbool.h>

#define MSR_IA32_FEATURE_CONTROL      0x03A
#define MSR_IA32_VMX_BASIC            0x480
#define MSR_IA32_VMX_PINBASED_CTLS    0x481
#define MSR_IA32_VMX_PROCBASED_CTLS   0x482
#define MSR_IA32_VMX_EXIT_CTLS        0x483
#define MSR_IA32_VMX_ENTRY_CTLS       0x484
#define MSR_IA32_VMX_MISC             0x485
#define MSR_IA32_VMX_CR0_FIXED0       0x486
#define MSR_IA32_VMX_CR0_FIXED1       0x487
#define MSR_IA32_VMX_CR4_FIXED0       0x488
#define MSR_IA32_VMX_CR4_FIXED1       0x489
#define MSR_IA32_VMX_PROCBASED_CTLS2  0x48B
#define MSR_IA32_VMX_EPT_VPID_CAP     0x48C
#define MSR_IA32_VMX_TRUE_PINBASED    0x48D
#define MSR_IA32_VMX_TRUE_PROCBASED   0x48E
#define MSR_IA32_VMX_TRUE_EXIT        0x48F
#define MSR_IA32_VMX_TRUE_ENTRY       0x490

#define FEATURE_CONTROL_LOCKED        (1ull << 0)
#define FEATURE_CONTROL_VMX_OUTSIDE   (1ull << 2)

#define VMCS_VPID                     0x0000
#define VMCS_GUEST_ES_SEL             0x0800
#define VMCS_GUEST_CS_SEL             0x0802
#define VMCS_GUEST_SS_SEL             0x0804
#define VMCS_GUEST_DS_SEL             0x0806
#define VMCS_GUEST_FS_SEL             0x0808
#define VMCS_GUEST_GS_SEL             0x080A
#define VMCS_GUEST_LDTR_SEL           0x080C
#define VMCS_GUEST_TR_SEL             0x080E
#define VMCS_HOST_ES_SEL              0x0C00
#define VMCS_HOST_CS_SEL              0x0C02
#define VMCS_HOST_SS_SEL              0x0C04
#define VMCS_HOST_DS_SEL              0x0C06
#define VMCS_HOST_FS_SEL              0x0C08
#define VMCS_HOST_GS_SEL              0x0C0A
#define VMCS_HOST_TR_SEL              0x0C0C

#define VMCS_IO_BITMAP_A              0x2000
#define VMCS_IO_BITMAP_B              0x2002
#define VMCS_MSR_BITMAP               0x2004
#define VMCS_EXIT_MSR_STORE_ADDR      0x2006
#define VMCS_EXIT_MSR_LOAD_ADDR       0x2008
#define VMCS_ENTRY_MSR_LOAD_ADDR      0x200A
#define VMCS_TSC_OFFSET               0x2010
#define VMCS_VIRTUAL_APIC_ADDR        0x2012
#define VMCS_EPT_POINTER              0x201A
#define VMCS_GUEST_PHYS_ADDR          0x2400
#define VMCS_LINK_POINTER             0x2800
#define VMCS_GUEST_DEBUGCTL           0x2802
#define VMCS_GUEST_PAT                0x2804
#define VMCS_GUEST_EFER               0x2806
#define VMCS_GUEST_PDPTE0             0x280A
#define VMCS_GUEST_PDPTE1             0x280C
#define VMCS_GUEST_PDPTE2             0x280E
#define VMCS_GUEST_PDPTE3             0x2810
#define VMCS_HOST_PAT                 0x2C00
#define VMCS_HOST_EFER                0x2C02

#define VMCS_PIN_CONTROLS             0x4000
#define VMCS_PROC_CONTROLS            0x4002
#define VMCS_EXCEPTION_BITMAP         0x4004
#define VMCS_PF_ERROR_MASK            0x4006
#define VMCS_PF_ERROR_MATCH           0x4008
#define VMCS_CR3_TARGET_COUNT         0x400A
#define VMCS_EXIT_CONTROLS            0x400C
#define VMCS_EXIT_MSR_STORE_COUNT     0x400E
#define VMCS_EXIT_MSR_LOAD_COUNT      0x4010
#define VMCS_ENTRY_CONTROLS           0x4012
#define VMCS_ENTRY_MSR_LOAD_COUNT     0x4014
#define VMCS_ENTRY_INTR_INFO          0x4016
#define VMCS_ENTRY_EXCEPTION_ERRCODE  0x4018
#define VMCS_ENTRY_INSTR_LEN          0x401A
#define VMCS_TPR_THRESHOLD            0x401C
#define VMCS_PROC_CONTROLS2           0x401E
#define VMCS_INSTR_ERROR              0x4400
#define VMCS_EXIT_REASON              0x4402
#define VMCS_EXIT_INTR_INFO           0x4404
#define VMCS_EXIT_INTR_ERRCODE        0x4406
#define VMCS_IDT_VECTORING_INFO       0x4408
#define VMCS_IDT_VECTORING_ERRCODE    0x440A
#define VMCS_EXIT_INSTR_LEN           0x440C
#define VMCS_EXIT_INSTR_INFO          0x440E
#define VMCS_GUEST_ES_LIMIT           0x4800
#define VMCS_GUEST_CS_LIMIT           0x4802
#define VMCS_GUEST_SS_LIMIT           0x4804
#define VMCS_GUEST_DS_LIMIT           0x4806
#define VMCS_GUEST_FS_LIMIT           0x4808
#define VMCS_GUEST_GS_LIMIT           0x480A
#define VMCS_GUEST_LDTR_LIMIT         0x480C
#define VMCS_GUEST_TR_LIMIT           0x480E
#define VMCS_GUEST_GDTR_LIMIT         0x4810
#define VMCS_GUEST_IDTR_LIMIT         0x4812
#define VMCS_GUEST_ES_AR              0x4814
#define VMCS_GUEST_CS_AR              0x4816
#define VMCS_GUEST_SS_AR              0x4818
#define VMCS_GUEST_DS_AR              0x481A
#define VMCS_GUEST_FS_AR              0x481C
#define VMCS_GUEST_GS_AR              0x481E
#define VMCS_GUEST_LDTR_AR            0x4820
#define VMCS_GUEST_TR_AR              0x4822
#define VMCS_GUEST_INTERRUPTIBILITY   0x4824
#define VMCS_GUEST_ACTIVITY_STATE     0x4826
#define VMCS_GUEST_SYSENTER_CS        0x482A
#define VMCS_HOST_SYSENTER_CS         0x4C00

#define VMCS_CR0_MASK                 0x6000
#define VMCS_CR4_MASK                 0x6002
#define VMCS_CR0_SHADOW               0x6004
#define VMCS_CR4_SHADOW               0x6006
#define VMCS_EXIT_QUALIFICATION       0x6400
#define VMCS_GUEST_LINEAR_ADDR        0x640A
#define VMCS_GUEST_CR0                0x6800
#define VMCS_GUEST_CR3                0x6802
#define VMCS_GUEST_CR4                0x6804
#define VMCS_GUEST_ES_BASE            0x6806
#define VMCS_GUEST_CS_BASE            0x6808
#define VMCS_GUEST_SS_BASE            0x680A
#define VMCS_GUEST_DS_BASE            0x680C
#define VMCS_GUEST_FS_BASE            0x680E
#define VMCS_GUEST_GS_BASE            0x6810
#define VMCS_GUEST_LDTR_BASE          0x6812
#define VMCS_GUEST_TR_BASE            0x6814
#define VMCS_GUEST_GDTR_BASE          0x6816
#define VMCS_GUEST_IDTR_BASE          0x6818
#define VMCS_GUEST_DR7                0x681A
#define VMCS_GUEST_RSP                0x681C
#define VMCS_GUEST_RIP                0x681E
#define VMCS_GUEST_RFLAGS             0x6820
#define VMCS_GUEST_PENDING_DBG        0x6822
#define VMCS_GUEST_SYSENTER_ESP       0x6824
#define VMCS_GUEST_SYSENTER_EIP       0x6826
#define VMCS_HOST_CR0                 0x6C00
#define VMCS_HOST_CR3                 0x6C02
#define VMCS_HOST_CR4                 0x6C04
#define VMCS_HOST_FS_BASE             0x6C06
#define VMCS_HOST_GS_BASE             0x6C08
#define VMCS_HOST_TR_BASE             0x6C0A
#define VMCS_HOST_GDTR_BASE           0x6C0C
#define VMCS_HOST_IDTR_BASE           0x6C0E
#define VMCS_HOST_SYSENTER_ESP        0x6C10
#define VMCS_HOST_SYSENTER_EIP        0x6C12
#define VMCS_HOST_RSP                 0x6C14
#define VMCS_HOST_RIP                 0x6C16

#define PIN_EXTINT_EXITING            (1u << 0)
#define PIN_NMI_EXITING               (1u << 3)
#define PIN_VIRTUAL_NMIS              (1u << 5)

#define PROC_INTR_WINDOW_EXITING      (1u << 2)
#define PROC_TSC_OFFSETTING           (1u << 3)
#define PROC_HLT_EXITING              (1u << 7)
#define PROC_INVLPG_EXITING           (1u << 9)
#define PROC_MWAIT_EXITING            (1u << 10)
#define PROC_RDPMC_EXITING            (1u << 11)
#define PROC_RDTSC_EXITING            (1u << 12)
#define PROC_CR3_LOAD_EXITING         (1u << 15)
#define PROC_CR3_STORE_EXITING        (1u << 16)
#define PROC_CR8_LOAD_EXITING         (1u << 19)
#define PROC_CR8_STORE_EXITING        (1u << 20)
#define PROC_TPR_SHADOW               (1u << 21)
#define PROC_NMI_WINDOW_EXITING       (1u << 22)
#define PROC_MOV_DR_EXITING           (1u << 23)
#define PROC_UNCOND_IO_EXITING        (1u << 24)
#define PROC_USE_IO_BITMAPS           (1u << 25)
#define PROC_USE_MSR_BITMAPS          (1u << 28)
#define PROC_MONITOR_EXITING          (1u << 29)
#define PROC_PAUSE_EXITING            (1u << 30)
#define PROC_SECONDARY                (1u << 31)

#define PROC2_ENABLE_EPT              (1u << 1)
#define PROC2_DESC_TABLE_EXITING      (1u << 2)
#define PROC2_ENABLE_RDTSCP           (1u << 3)
#define PROC2_ENABLE_VPID             (1u << 5)
#define PROC2_WBINVD_EXITING          (1u << 6)
#define PROC2_UNRESTRICTED_GUEST      (1u << 7)
#define PROC2_ENABLE_INVPCID          (1u << 12)
#define PROC2_ENABLE_XSAVES           (1u << 20)

#define EXIT_SAVE_DEBUG_CONTROLS      (1u << 2)
#define EXIT_HOST_ADDR_SPACE_SIZE     (1u << 9)
#define EXIT_ACK_INTR_ON_EXIT         (1u << 15)
#define EXIT_SAVE_PAT                 (1u << 18)
#define EXIT_LOAD_PAT                 (1u << 19)
#define EXIT_SAVE_EFER                (1u << 20)
#define EXIT_LOAD_EFER                (1u << 21)

#define ENTRY_LOAD_DEBUG_CONTROLS     (1u << 2)
#define ENTRY_IA32E_MODE              (1u << 9)
#define ENTRY_LOAD_PAT                (1u << 14)
#define ENTRY_LOAD_EFER               (1u << 15)

#define EXIT_REASON_EXCEPTION_NMI     0
#define EXIT_REASON_EXTERNAL_INT      1
#define EXIT_REASON_TRIPLE_FAULT      2
#define EXIT_REASON_INIT              3
#define EXIT_REASON_SIPI              4
#define EXIT_REASON_INTR_WINDOW       7
#define EXIT_REASON_NMI_WINDOW        8
#define EXIT_REASON_TASK_SWITCH       9
#define EXIT_REASON_CPUID             10
#define EXIT_REASON_GETSEC            11
#define EXIT_REASON_HLT               12
#define EXIT_REASON_INVD              13
#define EXIT_REASON_INVLPG            14
#define EXIT_REASON_RDPMC             15
#define EXIT_REASON_RDTSC             16
#define EXIT_REASON_RSM               17
#define EXIT_REASON_VMCALL            18
#define EXIT_REASON_VMCLEAR           19
#define EXIT_REASON_VMLAUNCH          20
#define EXIT_REASON_VMPTRLD           21
#define EXIT_REASON_VMPTRST           22
#define EXIT_REASON_VMREAD            23
#define EXIT_REASON_VMRESUME          24
#define EXIT_REASON_VMWRITE           25
#define EXIT_REASON_VMXOFF            26
#define EXIT_REASON_VMXON             27
#define EXIT_REASON_CR_ACCESS         28
#define EXIT_REASON_DR_ACCESS         29
#define EXIT_REASON_IO                30
#define EXIT_REASON_RDMSR             31
#define EXIT_REASON_WRMSR             32
#define EXIT_REASON_INVALID_STATE     33
#define EXIT_REASON_MSR_LOADING       34
#define EXIT_REASON_MWAIT             36
#define EXIT_REASON_MTF               37
#define EXIT_REASON_MONITOR           39
#define EXIT_REASON_PAUSE             40
#define EXIT_REASON_MCE_DURING_ENTRY  41
#define EXIT_REASON_TPR_BELOW         43
#define EXIT_REASON_APIC_ACCESS       44
#define EXIT_REASON_GDTR_IDTR         46
#define EXIT_REASON_LDTR_TR           47
#define EXIT_REASON_EPT_VIOLATION     48
#define EXIT_REASON_EPT_MISCONFIG     49
#define EXIT_REASON_INVEPT            50
#define EXIT_REASON_RDTSCP            51
#define EXIT_REASON_PREEMPT_TIMER     52
#define EXIT_REASON_INVVPID           53
#define EXIT_REASON_WBINVD            54
#define EXIT_REASON_XSETBV            55
#define EXIT_REASON_RDRAND            57
#define EXIT_REASON_INVPCID           58
#define EXIT_REASON_VMFUNC            59
#define EXIT_REASON_RDSEED            61
#define EXIT_REASON_XSAVES            63
#define EXIT_REASON_XRSTORS           64
#define EXIT_REASON_FAILED_ENTRY      (1u << 31)

#define INTR_INFO_VECTOR_MASK         0xFFu
#define INTR_INFO_TYPE_SHIFT          8
#define INTR_INFO_TYPE_MASK           (7u << 8)
#define INTR_TYPE_EXT_INTR            (0u << 8)
#define INTR_TYPE_NMI                 (2u << 8)
#define INTR_TYPE_HW_EXCEPTION        (3u << 8)
#define INTR_TYPE_SW_INTR             (4u << 8)
#define INTR_TYPE_PRIV_SW_EXCEPTION   (5u << 8)
#define INTR_TYPE_SW_EXCEPTION        (6u << 8)
#define INTR_INFO_DELIVER_CODE        (1u << 11)
#define INTR_INFO_NMI_UNBLOCK_IRET    (1u << 12)
#define INTR_INFO_VALID               (1u << 31)

#define INTERRUPTIBILITY_STI          (1u << 0)
#define INTERRUPTIBILITY_MOV_SS       (1u << 1)
#define INTERRUPTIBILITY_SMI          (1u << 2)
#define INTERRUPTIBILITY_NMI          (1u << 3)

#define ACTIVITY_ACTIVE               0
#define ACTIVITY_HLT                  1

#define AR_UNUSABLE                   (1u << 16)

#define EPT_READ                      (1ull << 0)
#define EPT_WRITE                     (1ull << 1)
#define EPT_EXEC                      (1ull << 2)
#define EPT_RWX                       (EPT_READ | EPT_WRITE | EPT_EXEC)
#define EPT_MT_WB                     (6ull << 3)
#define EPT_MT_UC                     (0ull << 3)
#define EPT_IGNORE_PAT                (1ull << 6)
#define EPT_LARGE                     (1ull << 7)
#define EPT_ADDR_MASK                 0x000FFFFFFFFFF000ull

#define EPT_VIOL_READ                 (1ull << 0)
#define EPT_VIOL_WRITE                (1ull << 1)
#define EPT_VIOL_FETCH                (1ull << 2)
#define EPT_VIOL_READABLE             (1ull << 3)
#define EPT_VIOL_WRITABLE             (1ull << 4)
#define EPT_VIOL_EXECUTABLE           (1ull << 5)
#define EPT_VIOL_NMI_UNBLOCK          (1ull << 12)

#define VMX_EPT_INVEPT_SINGLE         (1ull << 25)
#define VMX_EPT_INVEPT_ALL            (1ull << 26)
#define VMX_EPT_WB                    (1ull << 14)
#define VMX_EPT_4LEVEL                (1ull << 6)
#define VMX_VPID_INVVPID              (1ull << 32)
#define VMX_VPID_SINGLE               (1ull << 41)
#define VMX_VPID_ALL                  (1ull << 42)

static inline uint64_t vmx_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void vmx_wrmsr(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

static inline uint64_t vmread(uint64_t field) {
    uint64_t v;
    asm volatile("vmread %1, %0" : "=rm"(v) : "r"(field) : "cc");
    return v;
}

static inline bool vmwrite(uint64_t field, uint64_t value) {
    uint8_t err;
    asm volatile("vmwrite %2, %1; setna %0" : "=q"(err) : "r"(field), "rm"(value) : "cc");
    return !err;
}

static inline bool vmx_on(uint64_t phys) {
    uint8_t err;
    asm volatile("vmxon %1; setna %0" : "=q"(err) : "m"(phys) : "cc", "memory");
    return !err;
}

static inline void vmx_off(void) {
    asm volatile("vmxoff" ::: "cc", "memory");
}

static inline bool vmclear(uint64_t phys) {
    uint8_t err;
    asm volatile("vmclear %1; setna %0" : "=q"(err) : "m"(phys) : "cc", "memory");
    return !err;
}

static inline bool vmptrld(uint64_t phys) {
    uint8_t err;
    asm volatile("vmptrld %1; setna %0" : "=q"(err) : "m"(phys) : "cc", "memory");
    return !err;
}

static inline void invept(uint64_t type, uint64_t eptp) {
    struct { uint64_t eptp, zero; } d = { eptp, 0 };
    asm volatile("invept %0, %1" :: "m"(d), "r"(type) : "cc", "memory");
}

static inline void invvpid(uint64_t type, uint16_t vpid, uint64_t addr) {
    struct { uint64_t vpid, addr; } d = { vpid, addr };
    asm volatile("invvpid %0, %1" :: "m"(d), "r"(type) : "cc", "memory");
}

#endif
