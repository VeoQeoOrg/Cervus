#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/percpu.h"
#include "../../include/smp/smp.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"
#include "../../include/apic/apic.h"
#include "../../include/gdt/gdt.h"
#include "../../include/interrupts/idt.h"
#include "../../include/interrupts/interrupts.h"
#include "../../include/memory/tlb.h"
#include "../../include/signal/signal.h"
#include "../../include/syscall/errno.h"
#include "../../include/drivers/timer.h"
#include "../../include/sse/fpu.h"
#include <string.h>
#include <stdlib.h>

extern int  vmx_enter(vmx_regs_t *regs, int launched);
extern void vmx_exit_point(void);
extern tss_t *tss[MAX_CPUS];
extern uint32_t smp_get_lapic_id_for_cpu(uint32_t cpu_index);

#define CR0_PE (1ull << 0)
#define CR0_MP (1ull << 1)
#define CR0_EM (1ull << 2)
#define CR0_TS (1ull << 3)
#define CR0_ET (1ull << 4)
#define CR0_NE (1ull << 5)
#define CR0_WP (1ull << 16)
#define CR0_AM (1ull << 18)
#define CR0_NW (1ull << 29)
#define CR0_CD (1ull << 30)
#define CR0_PG (1ull << 31)

#define CR4_VME        (1ull << 0)
#define CR4_PVI        (1ull << 1)
#define CR4_TSD        (1ull << 2)
#define CR4_DE         (1ull << 3)
#define CR4_PSE        (1ull << 4)
#define CR4_PAE        (1ull << 5)
#define CR4_MCE        (1ull << 6)
#define CR4_PGE        (1ull << 7)
#define CR4_PCE        (1ull << 8)
#define CR4_OSFXSR     (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define CR4_UMIP       (1ull << 11)
#define CR4_VMXE       (1ull << 13)
#define CR4_SMXE       (1ull << 14)
#define CR4_FSGSBASE   (1ull << 16)
#define CR4_PCIDE      (1ull << 17)
#define CR4_OSXSAVE    (1ull << 18)
#define CR4_SMEP       (1ull << 20)
#define CR4_SMAP       (1ull << 21)

#define EFER_SCE (1ull << 0)
#define EFER_LME (1ull << 8)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)

#define RFLAGS_IF (1ull << 9)
#define RFLAGS_DF (1ull << 10)
#define RFLAGS_VM (1ull << 17)

#define MSR_IA32_TSC            0x010
#define MSR_IA32_PLATFORM_ID    0x017
#define MSR_IA32_APIC_BASE      0x01B
#define MSR_IA32_TSC_DEADLINE   0x6E0
#define MSR_IA32_TSC_ADJUST     0x03B
#define MSR_IA32_SPEC_CTRL      0x048
#define MSR_IA32_PRED_CMD       0x049
#define MSR_IA32_UCODE_REV      0x08B
#define MSR_MTRR_CAP            0x0FE
#define MSR_IA32_SYSENTER_CS    0x174
#define MSR_IA32_SYSENTER_ESP   0x175
#define MSR_IA32_SYSENTER_EIP   0x176
#define MSR_IA32_MCG_CAP        0x179
#define MSR_IA32_MCG_STATUS     0x17A
#define MSR_IA32_MCG_CTL        0x17B
#define MSR_IA32_MISC_ENABLE    0x1A0
#define MSR_IA32_DEBUGCTL       0x1D9
#define MSR_MTRR_PHYSBASE0      0x200
#define MSR_MTRR_FIX64K         0x250
#define MSR_MTRR_FIX16K_80000   0x258
#define MSR_MTRR_FIX16K_A0000   0x259
#define MSR_MTRR_FIX4K_C0000    0x268
#define MSR_IA32_PAT            0x277
#define MSR_MTRR_DEF_TYPE       0x2FF
#define MSR_IA32_MC0_CTL        0x400
#define MSR_EFER                0xC0000080
#define MSR_STAR                0xC0000081
#define MSR_LSTAR               0xC0000082
#define MSR_CSTAR               0xC0000083
#define MSR_SFMASK              0xC0000084
#define MSR_FS_BASE             0xC0000100
#define MSR_GS_BASE             0xC0000101
#define MSR_KERNEL_GS_BASE      0xC0000102
#define MSR_TSC_AUX             0xC0000103
#define MSR_KVM_WALL_CLOCK      0x11
#define MSR_KVM_SYSTEM_TIME     0x12
#define MSR_KVM_WALL_CLOCK_NEW  0x4b564d00
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01

#define APIC_BASE_ENABLE (1ull << 11)
#define APIC_BASE_EXTD   (1ull << 10)
#define APIC_BASE_BSP    (1ull << 8)
#define APIC_BASE_ADDR   0x000FFFFFFFFFF000ull

#define CR0_GUEST_OWNED CR0_TS
#define CR4_GUEST_OWNED (CR4_TSD | CR4_DE | CR4_PVI | CR4_PCE | CR4_OSFXSR | CR4_OSXMMEXCPT | CR4_PGE | CR4_FSGSBASE)

typedef struct {
    bool     probed;
    bool     ok;
    uint32_t revision;
    uint32_t pin, proc, proc2, exit, entry;
    uint64_t cr0_fixed0, cr0_fixed1, cr4_fixed0, cr4_fixed1;
    uint64_t ept_vpid;
    bool     ins_outs_info;
    bool     has_vpid;
    bool     preempt;
    uint32_t preempt_shift;
} vmx_caps_t;

static vmx_caps_t g_vmx;
static void      *g_vmxon[MAX_CPUS];
static bool       g_vmx_on[MAX_CPUS];
static uint64_t   g_cur_vmcs[MAX_CPUS];
static kvm_vcpu_t *volatile g_clear_req[MAX_CPUS];

#define IPI_VMCS_CLEAR 0x43
static spinlock_t g_vpid_lock = SPINLOCK_INIT;
static uint8_t    g_vpid_map[65536 / 8];

static inline uint64_t read_cr0(void) { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline uint64_t read_cr3(void) { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline uint64_t read_cr4(void) { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
static inline void write_cr0(uint64_t v) { asm volatile("mov %0, %%cr0" :: "r"(v) : "memory"); }
static inline void write_cr4(uint64_t v) { asm volatile("mov %0, %%cr4" :: "r"(v) : "memory"); }
static inline uint64_t rdtsc_now(void) { uint32_t lo, hi; asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static inline uint64_t irq_save(void) { uint64_t f; asm volatile("pushfq; pop %0; cli" : "=r"(f) :: "memory"); return f; }
static inline void irq_restore(uint64_t f) { if (f & RFLAGS_IF) asm volatile("sti" ::: "memory"); }

static void cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static bool adjust_ctl(uint32_t msr, uint32_t want, uint32_t opt, uint32_t *out) {
    uint64_t cap = vmx_rdmsr(msr);
    uint32_t must1 = (uint32_t)cap, may1 = (uint32_t)(cap >> 32);
    uint32_t v = (want | (opt & may1) | must1) & may1;
    if ((v & want) != want) return false;
    *out = v;
    return true;
}

static bool vmx_probe(void) {
    if (g_vmx.probed) return g_vmx.ok;
    g_vmx.probed = true;
    uint32_t a, b, c, d;
    cpuid_raw(1, 0, &a, &b, &c, &d);
    if (!(c & (1u << 5))) return false;

    uint64_t fc = vmx_rdmsr(MSR_IA32_FEATURE_CONTROL);
    if ((fc & FEATURE_CONTROL_LOCKED) && !(fc & FEATURE_CONTROL_VMX_OUTSIDE)) {
        serial_printf("[kvm] VMX is disabled by the firmware\n");
        return false;
    }

    uint64_t basic = vmx_rdmsr(MSR_IA32_VMX_BASIC);
    g_vmx.revision = (uint32_t)basic & 0x7FFFFFFFu;
    bool true_ctls = (basic >> 55) & 1;
    g_vmx.ins_outs_info = (basic >> 54) & 1;
    if (((basic >> 50) & 0xF) != 6) return false;

    if (!adjust_ctl(true_ctls ? MSR_IA32_VMX_TRUE_PINBASED : MSR_IA32_VMX_PINBASED_CTLS,
                    PIN_EXTINT_EXITING | PIN_NMI_EXITING, PIN_VIRTUAL_NMIS | PIN_PREEMPTION_TIMER, &g_vmx.pin))
        return false;
    g_vmx.preempt = (g_vmx.pin & PIN_PREEMPTION_TIMER) != 0;
    g_vmx.pin &= ~PIN_PREEMPTION_TIMER;
    g_vmx.preempt_shift = (uint32_t)vmx_rdmsr(MSR_IA32_VMX_MISC) & 0x1F;
    if (!adjust_ctl(true_ctls ? MSR_IA32_VMX_TRUE_PROCBASED : MSR_IA32_VMX_PROCBASED_CTLS,
                    PROC_HLT_EXITING | PROC_CR8_LOAD_EXITING | PROC_CR8_STORE_EXITING |
                    PROC_UNCOND_IO_EXITING | PROC_MOV_DR_EXITING | PROC_SECONDARY |
                    PROC_TSC_OFFSETTING | PROC_MWAIT_EXITING | PROC_MONITOR_EXITING |
                    PROC_RDPMC_EXITING, 0, &g_vmx.proc))
        return false;
    if (!adjust_ctl(MSR_IA32_VMX_PROCBASED_CTLS2, PROC2_ENABLE_EPT | PROC2_UNRESTRICTED_GUEST,
                    PROC2_ENABLE_VPID | PROC2_ENABLE_RDTSCP | PROC2_ENABLE_INVPCID, &g_vmx.proc2))
        return false;
    if (!adjust_ctl(true_ctls ? MSR_IA32_VMX_TRUE_EXIT : MSR_IA32_VMX_EXIT_CTLS,
                    EXIT_HOST_ADDR_SPACE_SIZE | EXIT_SAVE_EFER | EXIT_LOAD_EFER |
                    EXIT_SAVE_PAT | EXIT_LOAD_PAT, EXIT_SAVE_DEBUG_CONTROLS, &g_vmx.exit))
        return false;
    if (!adjust_ctl(true_ctls ? MSR_IA32_VMX_TRUE_ENTRY : MSR_IA32_VMX_ENTRY_CTLS,
                    ENTRY_LOAD_EFER | ENTRY_LOAD_PAT, ENTRY_LOAD_DEBUG_CONTROLS, &g_vmx.entry))
        return false;
    g_vmx.entry &= ~ENTRY_IA32E_MODE;
    g_vmx.proc &= ~(PROC_INTR_WINDOW_EXITING | PROC_NMI_WINDOW_EXITING);

    g_vmx.ept_vpid = vmx_rdmsr(MSR_IA32_VMX_EPT_VPID_CAP);
    if (!(g_vmx.ept_vpid & VMX_EPT_4LEVEL) || !(g_vmx.ept_vpid & VMX_EPT_WB) ||
        !(g_vmx.ept_vpid & (VMX_EPT_INVEPT_SINGLE | VMX_EPT_INVEPT_ALL)))
        return false;
    g_vmx.has_vpid = (g_vmx.proc2 & PROC2_ENABLE_VPID) &&
                     (g_vmx.ept_vpid & VMX_VPID_INVVPID) &&
                     (g_vmx.ept_vpid & (VMX_VPID_SINGLE | VMX_VPID_ALL));
    if (!g_vmx.has_vpid) g_vmx.proc2 &= ~PROC2_ENABLE_VPID;

    g_vmx.cr0_fixed0 = vmx_rdmsr(MSR_IA32_VMX_CR0_FIXED0);
    g_vmx.cr0_fixed1 = vmx_rdmsr(MSR_IA32_VMX_CR0_FIXED1);
    g_vmx.cr4_fixed0 = vmx_rdmsr(MSR_IA32_VMX_CR4_FIXED0);
    g_vmx.cr4_fixed1 = vmx_rdmsr(MSR_IA32_VMX_CR4_FIXED1);

    uint32_t n = smp_get_cpu_count();
    for (uint32_t i = 0; i < n && i < MAX_CPUS; i++) {
        g_vmxon[i] = pmm_alloc_zero(1);
        if (!g_vmxon[i]) return false;
        *(uint32_t *)g_vmxon[i] = g_vmx.revision;
    }
    g_vpid_map[0] = 1;
    g_vmx.ok = true;
    serial_printf("[kvm] VMX: EPT, unrestricted guest%s%s%s\n",
                  g_vmx.has_vpid ? ", VPID" : "",
                  (g_vmx.proc2 & PROC2_ENABLE_RDTSCP) ? ", RDTSCP" : "",
                  g_vmx.preempt ? ", preemption timer" : "");
    return true;
}

bool vmx_supported(void) { return vmx_probe(); }

static bool vmx_cpu_enable(uint32_t cpu) {
    if (g_vmx_on[cpu]) return true;
    if (!g_vmxon[cpu]) return false;
    uint64_t fc = vmx_rdmsr(MSR_IA32_FEATURE_CONTROL);
    if (!(fc & FEATURE_CONTROL_LOCKED))
        vmx_wrmsr(MSR_IA32_FEATURE_CONTROL, fc | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMX_OUTSIDE);
    write_cr0((read_cr0() | g_vmx.cr0_fixed0) & g_vmx.cr0_fixed1);
    write_cr4((read_cr4() | CR4_VMXE | g_vmx.cr4_fixed0) & g_vmx.cr4_fixed1);
    if (!vmx_on(pmm_virt_to_phys(g_vmxon[cpu]))) {
        serial_printf("[kvm] VMXON failed on cpu %u\n", cpu);
        return false;
    }
    if (g_vmx.ept_vpid & VMX_EPT_INVEPT_ALL) invept(2, 0);
    if (g_vmx.has_vpid && (g_vmx.ept_vpid & VMX_VPID_ALL)) invvpid(2, 0, 0);
    g_vmx_on[cpu] = true;
    return true;
}

static uint64_t vmx_supported_cr4(void) {
    uint64_t s = CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | CR4_PSE | CR4_PAE | CR4_MCE | CR4_PGE |
                 CR4_PCE | CR4_OSFXSR | CR4_OSXMMEXCPT;
    uint64_t host = read_cr4();
    s |= host & (CR4_FSGSBASE | CR4_PCIDE | CR4_SMEP | CR4_SMAP | CR4_UMIP);
    if (fpu_xcr0) s |= CR4_OSXSAVE;
    uint32_t a, b, c, d;
    cpuid_raw(7, 0, &a, &b, &c, &d);
    if (b & (1u << 0)) s |= CR4_FSGSBASE;
    if (b & (1u << 7)) s |= CR4_SMEP;
    if (b & (1u << 20)) s |= CR4_SMAP;
    cpuid_raw(1, 0, &a, &b, &c, &d);
    if (c & (1u << 17)) s |= CR4_PCIDE;
    return s & g_vmx.cr4_fixed1;
}

static uint16_t vpid_alloc(void) {
    if (!g_vmx.has_vpid) return 0;
    uint64_t f = spinlock_acquire_irqsave(&g_vpid_lock);
    uint16_t v = 0;
    for (uint32_t i = 1; i < 65536; i++) {
        if (!(g_vpid_map[i >> 3] & (1u << (i & 7)))) {
            g_vpid_map[i >> 3] |= (uint8_t)(1u << (i & 7));
            v = (uint16_t)i;
            break;
        }
    }
    spinlock_release_irqrestore(&g_vpid_lock, f);
    return v;
}

static void vpid_free(uint16_t v) {
    if (!v) return;
    uint64_t f = spinlock_acquire_irqsave(&g_vpid_lock);
    g_vpid_map[v >> 3] &= (uint8_t)~(1u << (v & 7));
    spinlock_release_irqrestore(&g_vpid_lock, f);
}

static void vmx_flush_vpid(kvm_vcpu_t *vcpu) {
    if (!vcpu->vpid) return;
    if (g_vmx.ept_vpid & VMX_VPID_SINGLE) invvpid(1, vcpu->vpid, 0);
    else invvpid(2, 0, 0);
}

static void vmx_flush_ept(kvm_vcpu_t *vcpu) {
    if (g_vmx.ept_vpid & VMX_EPT_INVEPT_SINGLE) invept(1, vcpu->vm->eptp);
    else invept(2, 0);
}

static uint64_t guest_cr0(kvm_vcpu_t *vcpu) {
    return (vcpu->cr0 & ~CR0_GUEST_OWNED) | (vmread(VMCS_GUEST_CR0) & CR0_GUEST_OWNED);
}

static uint64_t guest_cr4(kvm_vcpu_t *vcpu) {
    return (vcpu->cr4 & ~CR4_GUEST_OWNED) | (vmread(VMCS_GUEST_CR4) & CR4_GUEST_OWNED);
}

static void load_pdptes(kvm_vcpu_t *vcpu) {
    uint64_t cr0 = vcpu->cr0, cr4 = vcpu->cr4;
    if (!(cr0 & CR0_PG) || !(cr4 & CR4_PAE) || (vcpu->efer & EFER_LMA)) return;
    uint64_t pdpte[4] = { 0, 0, 0, 0 };
    uint64_t cr3 = vmread(VMCS_GUEST_CR3) & 0xFFFFFFE0ull;
    kvm_read_guest(vcpu->vm, cr3, pdpte, sizeof pdpte);
    vmwrite(VMCS_GUEST_PDPTE0, pdpte[0]);
    vmwrite(VMCS_GUEST_PDPTE1, pdpte[1]);
    vmwrite(VMCS_GUEST_PDPTE2, pdpte[2]);
    vmwrite(VMCS_GUEST_PDPTE3, pdpte[3]);
}

static void update_entry_controls(kvm_vcpu_t *vcpu) {
    uint32_t e = g_vmx.entry;
    if (vcpu->efer & EFER_LMA) e |= ENTRY_IA32E_MODE;
    vmwrite(VMCS_ENTRY_CONTROLS, e);
    vmwrite(VMCS_GUEST_EFER, vcpu->efer);
}

static void apply_cr0(kvm_vcpu_t *vcpu, uint64_t val) {
    uint64_t old = vcpu->cr0;
    if (!(old & CR0_PG) && (val & CR0_PG) && (vcpu->efer & EFER_LME)) vcpu->efer |= EFER_LMA;
    if ((old & CR0_PG) && !(val & CR0_PG)) vcpu->efer &= ~EFER_LMA;
    vcpu->cr0 = val;
    uint64_t fixed0 = g_vmx.cr0_fixed0 & ~(CR0_PE | CR0_PG);
    uint64_t hw = ((val & ~(CR0_CD | CR0_NW)) | fixed0 | CR0_NE) & g_vmx.cr0_fixed1;
    vmwrite(VMCS_GUEST_CR0, hw);
    vmwrite(VMCS_CR0_SHADOW, val);
    update_entry_controls(vcpu);
    load_pdptes(vcpu);
}

static void apply_cr4(kvm_vcpu_t *vcpu, uint64_t val) {
    vcpu->cr4 = val;
    uint64_t hw = (val | CR4_VMXE | g_vmx.cr4_fixed0) & g_vmx.cr4_fixed1;
    vmwrite(VMCS_GUEST_CR4, hw);
    vmwrite(VMCS_CR4_SHADOW, val);
    load_pdptes(vcpu);
}

static int set_cr0_checked(kvm_vcpu_t *vcpu, uint64_t val) {
    if (val >> 32) return -1;
    if ((val & CR0_PG) && !(val & CR0_PE)) return -1;
    if ((val & CR0_NW) && !(val & CR0_CD)) return -1;
    if ((val & CR0_PG) && !(vcpu->cr0 & CR0_PG) && (vcpu->efer & EFER_LME) && !(vcpu->cr4 & CR4_PAE))
        return -1;
    apply_cr0(vcpu, val | CR0_ET);
    return 0;
}

static int set_cr4_checked(kvm_vcpu_t *vcpu, uint64_t val) {
    if (val & ~vmx_supported_cr4()) return -1;
    if ((vcpu->efer & EFER_LMA) && !(val & CR4_PAE)) return -1;
    if ((val & CR4_PCIDE) && !(vcpu->cr4 & CR4_PCIDE) && (vmread(VMCS_GUEST_CR3) & 0xFFF)) return -1;
    apply_cr4(vcpu, val);
    return 0;
}

static uint32_t seg_to_ar(const struct kvm_segment *s, bool is_ldt) {
    if (s->unusable || !s->present) return AR_UNUSABLE;
    uint32_t type = s->type & 0xF;
    if (!is_ldt && s->s) type |= 1;
    return type | ((uint32_t)(s->s & 1) << 4) | ((uint32_t)(s->dpl & 3) << 5) |
           ((uint32_t)(s->present & 1) << 7) | ((uint32_t)(s->avl & 1) << 12) |
           ((uint32_t)(s->l & 1) << 13) | ((uint32_t)(s->db & 1) << 14) | ((uint32_t)(s->g & 1) << 15);
}

static void ar_to_seg(uint32_t ar, struct kvm_segment *s) {
    s->unusable = (ar & AR_UNUSABLE) ? 1 : 0;
    s->type = ar & 0xF;
    s->s = (ar >> 4) & 1;
    s->dpl = (ar >> 5) & 3;
    s->present = s->unusable ? 0 : ((ar >> 7) & 1);
    s->avl = (ar >> 12) & 1;
    s->l = (ar >> 13) & 1;
    s->db = (ar >> 14) & 1;
    s->g = (ar >> 15) & 1;
    s->padding = 0;
}

typedef struct { uint32_t sel, base, limit, ar; } seg_fields_t;

static const seg_fields_t g_seg_fields[] = {
    { VMCS_GUEST_ES_SEL, VMCS_GUEST_ES_BASE, VMCS_GUEST_ES_LIMIT, VMCS_GUEST_ES_AR },
    { VMCS_GUEST_CS_SEL, VMCS_GUEST_CS_BASE, VMCS_GUEST_CS_LIMIT, VMCS_GUEST_CS_AR },
    { VMCS_GUEST_SS_SEL, VMCS_GUEST_SS_BASE, VMCS_GUEST_SS_LIMIT, VMCS_GUEST_SS_AR },
    { VMCS_GUEST_DS_SEL, VMCS_GUEST_DS_BASE, VMCS_GUEST_DS_LIMIT, VMCS_GUEST_DS_AR },
    { VMCS_GUEST_FS_SEL, VMCS_GUEST_FS_BASE, VMCS_GUEST_FS_LIMIT, VMCS_GUEST_FS_AR },
    { VMCS_GUEST_GS_SEL, VMCS_GUEST_GS_BASE, VMCS_GUEST_GS_LIMIT, VMCS_GUEST_GS_AR },
    { VMCS_GUEST_LDTR_SEL, VMCS_GUEST_LDTR_BASE, VMCS_GUEST_LDTR_LIMIT, VMCS_GUEST_LDTR_AR },
    { VMCS_GUEST_TR_SEL, VMCS_GUEST_TR_BASE, VMCS_GUEST_TR_LIMIT, VMCS_GUEST_TR_AR },
};

enum { SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS, SEG_LDTR, SEG_TR };

static void write_seg(int idx, const struct kvm_segment *s) {
    const seg_fields_t *f = &g_seg_fields[idx];
    vmwrite(f->sel, s->selector);
    vmwrite(f->base, s->base);
    vmwrite(f->limit, s->limit);
    vmwrite(f->ar, seg_to_ar(s, idx == SEG_LDTR));
}

static void read_seg(int idx, struct kvm_segment *s) {
    const seg_fields_t *f = &g_seg_fields[idx];
    memset(s, 0, sizeof *s);
    s->selector = (uint16_t)vmread(f->sel);
    s->base = vmread(f->base);
    s->limit = (uint32_t)vmread(f->limit);
    ar_to_seg((uint32_t)vmread(f->ar), s);
}

static uint64_t vmx_reg_read(kvm_vcpu_t *vcpu, int reg) {
    if (reg == VCPU_REG_RSP) return vmread(VMCS_GUEST_RSP);
    return vcpu->regs.gpr[reg & 15];
}

static void vmx_reg_write(kvm_vcpu_t *vcpu, int reg, uint64_t val) {
    if (reg == VCPU_REG_RSP) vmwrite(VMCS_GUEST_RSP, val);
    else vcpu->regs.gpr[reg & 15] = val;
}

static void vmcs_setup(kvm_vcpu_t *vcpu) {
    vmwrite(VMCS_PIN_CONTROLS, g_vmx.pin);
    vmwrite(VMCS_PROC_CONTROLS, g_vmx.proc);
    vmwrite(VMCS_PROC_CONTROLS2, g_vmx.proc2);
    vmwrite(VMCS_EXIT_CONTROLS, g_vmx.exit);
    vmwrite(VMCS_ENTRY_CONTROLS, g_vmx.entry);
    vmwrite(VMCS_EXCEPTION_BITMAP, 0);
    vmwrite(VMCS_PF_ERROR_MASK, 0);
    vmwrite(VMCS_PF_ERROR_MATCH, 0);
    vmwrite(VMCS_CR3_TARGET_COUNT, 0);
    vmwrite(VMCS_EXIT_MSR_STORE_COUNT, 0);
    vmwrite(VMCS_EXIT_MSR_LOAD_COUNT, 0);
    vmwrite(VMCS_ENTRY_MSR_LOAD_COUNT, 0);
    vmwrite(VMCS_ENTRY_INTR_INFO, 0);
    vmwrite(VMCS_LINK_POINTER, ~0ull);
    vmwrite(VMCS_EPT_POINTER, vcpu->vm->eptp);
    if (vcpu->vpid) vmwrite(VMCS_VPID, vcpu->vpid);
    vcpu->tsc_offset = vcpu->vm->tsc_offset;
    vmwrite(VMCS_TSC_OFFSET, (uint64_t)vcpu->tsc_offset);
    vmwrite(VMCS_CR0_MASK, ~CR0_GUEST_OWNED);
    vmwrite(VMCS_CR4_MASK, ~CR4_GUEST_OWNED);

    vmwrite(VMCS_HOST_CR0, read_cr0());
    vmwrite(VMCS_HOST_CR4, read_cr4());
    vmwrite(VMCS_HOST_CS_SEL, GDT_CODE_SEGMENT);
    vmwrite(VMCS_HOST_SS_SEL, GDT_DATA_SEGMENT);
    vmwrite(VMCS_HOST_DS_SEL, GDT_DATA_SEGMENT);
    vmwrite(VMCS_HOST_ES_SEL, GDT_DATA_SEGMENT);
    vmwrite(VMCS_HOST_FS_SEL, 0);
    vmwrite(VMCS_HOST_GS_SEL, 0);
    vmwrite(VMCS_HOST_IDTR_BASE, (uint64_t)idtr.base);
    vmwrite(VMCS_HOST_GDTR_BASE, (uint64_t)gdtr.pointer);
    vmwrite(VMCS_HOST_SYSENTER_CS, vmx_rdmsr(MSR_IA32_SYSENTER_CS));
    vmwrite(VMCS_HOST_SYSENTER_ESP, vmx_rdmsr(MSR_IA32_SYSENTER_ESP));
    vmwrite(VMCS_HOST_SYSENTER_EIP, vmx_rdmsr(MSR_IA32_SYSENTER_EIP));
    vmwrite(VMCS_HOST_EFER, vmx_rdmsr(MSR_EFER));
    vmwrite(VMCS_HOST_PAT, vmx_rdmsr(MSR_IA32_PAT));
    vmwrite(VMCS_HOST_RIP, (uint64_t)vmx_exit_point);
}

static void vmcs_reset_guest(kvm_vcpu_t *vcpu) {
    struct kvm_segment cs = { .base = 0xFFFF0000ull, .limit = 0xFFFF, .selector = 0xF000,
                              .type = 0xB, .present = 1, .s = 1 };
    struct kvm_segment ds = { .base = 0, .limit = 0xFFFF, .selector = 0, .type = 3, .present = 1, .s = 1 };
    struct kvm_segment ldt = { .base = 0, .limit = 0xFFFF, .type = 2, .present = 1 };
    struct kvm_segment tr = { .base = 0, .limit = 0xFFFF, .type = 0xB, .present = 1 };
    write_seg(SEG_CS, &cs);
    write_seg(SEG_DS, &ds);
    write_seg(SEG_ES, &ds);
    write_seg(SEG_SS, &ds);
    write_seg(SEG_FS, &ds);
    write_seg(SEG_GS, &ds);
    write_seg(SEG_LDTR, &ldt);
    write_seg(SEG_TR, &tr);
    vmwrite(VMCS_GUEST_GDTR_BASE, 0);
    vmwrite(VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
    vmwrite(VMCS_GUEST_IDTR_BASE, 0);
    vmwrite(VMCS_GUEST_IDTR_LIMIT, 0xFFFF);
    vcpu->efer = 0;
    vcpu->cr4 = 0;
    apply_cr4(vcpu, 0);
    vcpu->cr0 = 0;
    apply_cr0(vcpu, 0x60000010ull);
    vmwrite(VMCS_GUEST_CR3, 0);
    vmwrite(VMCS_GUEST_DR7, 0x400);
    vmwrite(VMCS_GUEST_DEBUGCTL, 0);
    vmwrite(VMCS_GUEST_PAT, 0x0007040600070406ull);
    vmwrite(VMCS_GUEST_RSP, 0);
    vmwrite(VMCS_GUEST_RIP, 0xFFF0);
    vmwrite(VMCS_GUEST_RFLAGS, 2);
    vmwrite(VMCS_GUEST_PENDING_DBG, 0);
    vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    vmwrite(VMCS_GUEST_ACTIVITY_STATE, ACTIVITY_ACTIVE);
    vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
    vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
    vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);
    memset(&vcpu->regs, 0, sizeof vcpu->regs);
    vcpu->regs.gpr[VCPU_REG_RDX] = 0x600;
}

static void vmcs_drop_here(kvm_vcpu_t *vcpu, uint32_t cpu) {
    vmclear(vcpu->vmcs_phys);
    if (g_cur_vmcs[cpu] == vcpu->vmcs_phys) g_cur_vmcs[cpu] = 0;
    vcpu->launched = false;
    __atomic_store_n(&vcpu->loaded_cpu, -1, __ATOMIC_RELEASE);
}

static void serve_clear(void) {
    uint32_t cpu = smp_cpu_index();
    kvm_vcpu_t *v = __atomic_load_n(&g_clear_req[cpu], __ATOMIC_ACQUIRE);
    if (!v) return;
    if (__atomic_load_n(&v->loaded_cpu, __ATOMIC_ACQUIRE) == (int)cpu) vmcs_drop_here(v, cpu);
    __atomic_store_n(&g_clear_req[cpu], NULL, __ATOMIC_RELEASE);
}

DEFINE_IRQ(IPI_VMCS_CLEAR, ipi_vmcs_clear_handler)
{
    (void)frame;
    serve_clear();
    lapic_eoi();
}

static void vmcs_claim(kvm_vcpu_t *vcpu) {
    for (;;) {
        asm volatile("cli" ::: "memory");
        uint32_t cpu = smp_cpu_index();
        int c = __atomic_load_n(&vcpu->loaded_cpu, __ATOMIC_ACQUIRE);
        if (c < 0 || c == (int)cpu) return;
        asm volatile("sti" ::: "memory");
        kvm_vcpu_t *none = NULL;
        while (!__atomic_compare_exchange_n(&g_clear_req[c], &none, vcpu, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            none = NULL;
            asm volatile("pause");
        }
        lapic_send_ipi(smp_get_lapic_id_for_cpu((uint32_t)c), IPI_VMCS_CLEAR);
        while (__atomic_load_n(&g_clear_req[c], __ATOMIC_ACQUIRE) == vcpu) {
            tlb_poll();
            asm volatile("pause");
        }
    }
}

static void vmcs_load(kvm_vcpu_t *vcpu);

static void vmx_vmcs_enter(kvm_vcpu_t *vcpu) {
    vmcs_claim(vcpu);
    spin_depth_inc();
    vmcs_load(vcpu);
}

static void vmx_vmcs_leave(kvm_vcpu_t *vcpu) {
    (void)vcpu;
    spin_depth_dec();
    asm volatile("sti" ::: "memory");
}

static int vmx_vcpu_create(kvm_vcpu_t *vcpu) {
    vcpu->loaded_cpu = -1;
    if (!vmx_probe()) return -ENODEV;
    vcpu->vmcs = pmm_alloc_zero(1);
    vcpu->guest_fpu = pmm_alloc_zero(2);
    if (!vcpu->vmcs || !vcpu->guest_fpu) return -ENOMEM;
    vcpu->host_fpu = vcpu->guest_fpu + 4096;
    *(uint32_t *)vcpu->vmcs = g_vmx.revision;
    vcpu->vmcs_phys = pmm_virt_to_phys(vcpu->vmcs);
    vcpu->loaded_cpu = -1;
    vcpu->last_cpu = -1;
    vcpu->launched = false;
    vcpu->vpid = vpid_alloc();
    vcpu->mcg_cap = KVM_MCE_BANKS;
    kvm_vcpu_reset_soft(vcpu);
    asm volatile("cli" ::: "memory");
    if (!vmx_cpu_enable(smp_cpu_index()) || !vmclear(vcpu->vmcs_phys)) {
        asm volatile("sti" ::: "memory");
        return -EIO;
    }
    asm volatile("sti" ::: "memory");
    vmx_vmcs_enter(vcpu);
    vmcs_setup(vcpu);
    vmcs_reset_guest(vcpu);
    vmx_vmcs_leave(vcpu);
    return 0;
}

static void vmx_vcpu_destroy(kvm_vcpu_t *vcpu) {
    if (vcpu->vmcs && vcpu->loaded_cpu >= 0) {
        vmcs_claim(vcpu);
        if (vcpu->loaded_cpu == (int)smp_cpu_index()) vmcs_drop_here(vcpu, smp_cpu_index());
        asm volatile("sti" ::: "memory");
    }
    if (vcpu->vmcs) pmm_free(vcpu->vmcs, 1);
    if (vcpu->guest_fpu) pmm_free(vcpu->guest_fpu, 2);
    vpid_free(vcpu->vpid);
    vcpu->vmcs = NULL;
    vcpu->guest_fpu = NULL;
}

static void guest_state_load(kvm_vcpu_t *vcpu) {
    if (vcpu->guest_loaded) return;
    kvm_guest_fpu_load(vcpu);
    vcpu->host_star = vmx_rdmsr(MSR_STAR);
    vcpu->host_lstar = vmx_rdmsr(MSR_LSTAR);
    vcpu->host_cstar = vmx_rdmsr(MSR_CSTAR);
    vcpu->host_sfmask = vmx_rdmsr(MSR_SFMASK);
    vcpu->host_kernel_gs_base = vmx_rdmsr(MSR_KERNEL_GS_BASE);
    vmx_wrmsr(MSR_STAR, vcpu->msr_star);
    vmx_wrmsr(MSR_LSTAR, vcpu->msr_lstar);
    vmx_wrmsr(MSR_CSTAR, vcpu->msr_cstar);
    vmx_wrmsr(MSR_SFMASK, vcpu->msr_sfmask);
    vmx_wrmsr(MSR_KERNEL_GS_BASE, vcpu->msr_kernel_gs_base);
    if (g_vmx.proc2 & PROC2_ENABLE_RDTSCP) {
        vcpu->host_tsc_aux = vmx_rdmsr(MSR_TSC_AUX);
        vmx_wrmsr(MSR_TSC_AUX, vcpu->msr_tsc_aux);
    }
    vcpu->guest_loaded = true;
}

static void guest_state_put(kvm_vcpu_t *vcpu) {
    if (!vcpu->guest_loaded) return;
    kvm_guest_fpu_put(vcpu);
    vcpu->msr_kernel_gs_base = vmx_rdmsr(MSR_KERNEL_GS_BASE);
    vmx_wrmsr(MSR_STAR, vcpu->host_star);
    vmx_wrmsr(MSR_LSTAR, vcpu->host_lstar);
    vmx_wrmsr(MSR_CSTAR, vcpu->host_cstar);
    vmx_wrmsr(MSR_SFMASK, vcpu->host_sfmask);
    vmx_wrmsr(MSR_KERNEL_GS_BASE, vcpu->host_kernel_gs_base);
    if (g_vmx.proc2 & PROC2_ENABLE_RDTSCP) vmx_wrmsr(MSR_TSC_AUX, vcpu->host_tsc_aux);
    vcpu->guest_loaded = false;
}

static void vmx_sched_out(kvm_vcpu_t *vcpu) {
    guest_state_put(vcpu);
    uint32_t cpu = smp_cpu_index();
    if (vcpu->loaded_cpu == (int)cpu) vmcs_drop_here(vcpu, cpu);
}

static void vmcs_load(kvm_vcpu_t *vcpu) {
    uint32_t cpu = smp_cpu_index();
    if (vcpu->loaded_cpu == (int)cpu) {
        if (g_cur_vmcs[cpu] != vcpu->vmcs_phys) {
            vmptrld(vcpu->vmcs_phys);
            g_cur_vmcs[cpu] = vcpu->vmcs_phys;
        }
        return;
    }
    vmx_cpu_enable(cpu);
    vmptrld(vcpu->vmcs_phys);
    g_cur_vmcs[cpu] = vcpu->vmcs_phys;
    vcpu->launched = false;
    __atomic_store_n(&vcpu->loaded_cpu, (int)cpu, __ATOMIC_RELEASE);
    vmwrite(VMCS_HOST_TR_SEL, TSS_SELECTOR_BASE + cpu * 0x10);
    vmwrite(VMCS_HOST_TR_BASE, (uint64_t)tss[cpu]);
    vmwrite(VMCS_HOST_GS_BASE, (uint64_t)percpu_regions[cpu]);
    if (vcpu->last_cpu != (int)cpu) {
        vcpu->last_cpu = (int)cpu;
        vmx_flush_vpid(vcpu);
        vmx_flush_ept(vcpu);
        vcpu->tlb_gen_seen = vcpu->vm->tlb_gen;
    }
}

static void vmx_sched_in(kvm_vcpu_t *vcpu) {
    vmcs_load(vcpu);
}

static void vcpu_prepare_entry(kvm_vcpu_t *vcpu) {
    vmcs_load(vcpu);
    vmwrite(VMCS_HOST_CR3, read_cr3());
    vmwrite(VMCS_HOST_FS_BASE, vmx_rdmsr(MSR_FS_BASE));
    guest_state_load(vcpu);
}

static void vmx_kick(kvm_vcpu_t *vcpu) {
    int cpu = vcpu->loaded_cpu;
    if (vcpu->mode == VCPU_IN_GUEST && cpu >= 0 && (uint32_t)cpu != smp_cpu_index())
        ipi_reschedule_cpu(smp_get_lapic_id_for_cpu((uint32_t)cpu));
}

static void skip_instruction(kvm_vcpu_t *vcpu) {
    uint64_t rip = vmread(VMCS_GUEST_RIP) + vmread(VMCS_EXIT_INSTR_LEN);
    if (!(vcpu->efer & EFER_LMA) || !((vmread(VMCS_GUEST_CS_AR) >> 13) & 1)) rip &= 0xFFFFFFFFull;
    vmwrite(VMCS_GUEST_RIP, rip);
    uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
    if (intr & (INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS))
        vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr & ~(INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS));
}

static bool guest_interruptible(kvm_vcpu_t *vcpu) {
    (void)vcpu;
    if (!(vmread(VMCS_GUEST_RFLAGS) & RFLAGS_IF)) return false;
    if (vmread(VMCS_GUEST_INTERRUPTIBILITY) & (INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS)) return false;
    return true;
}

static bool ready_for_injection(kvm_vcpu_t *vcpu) {
    if (vmread(VMCS_ENTRY_INTR_INFO) & INTR_INFO_VALID) return false;
    if (vcpu->exc_pending || vcpu->pending_irq >= 0) return false;
    if (!kvm_accepts_extint(vcpu)) return false;
    return guest_interruptible(vcpu);
}

static void set_proc_bit(uint32_t bit, bool on) {
    uint32_t p = (uint32_t)vmread(VMCS_PROC_CONTROLS);
    uint32_t n = on ? (p | bit) : (p & ~bit);
    if (n != p) vmwrite(VMCS_PROC_CONTROLS, n);
}

static void inject_events(kvm_vcpu_t *vcpu) {
    bool irqchip = vcpu->vm->irqchip_split;
    bool busy = (vmread(VMCS_ENTRY_INTR_INFO) & INTR_INFO_VALID) != 0;
    if (!busy && vcpu->exc_pending) {
        uint32_t info = INTR_INFO_VALID | vcpu->exc_vector;
        if (vcpu->exc_vector == 3 || vcpu->exc_vector == 4) {
            info |= INTR_TYPE_SW_EXCEPTION;
            vmwrite(VMCS_ENTRY_INSTR_LEN, 1);
        } else {
            info |= INTR_TYPE_HW_EXCEPTION;
        }
        if (vcpu->exc_has_error && (vcpu->cr0 & CR0_PE)) {
            info |= INTR_INFO_DELIVER_CODE;
            vmwrite(VMCS_ENTRY_EXCEPTION_ERRCODE, vcpu->exc_error);
        }
        vmwrite(VMCS_ENTRY_INTR_INFO, info);
        vcpu->exc_pending = false;
        busy = true;
    }
    if (vcpu->nmi_pending) {
        uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
        if (!busy && !(intr & (INTERRUPTIBILITY_NMI | INTERRUPTIBILITY_MOV_SS | INTERRUPTIBILITY_STI))) {
            vmwrite(VMCS_ENTRY_INTR_INFO, INTR_INFO_VALID | INTR_TYPE_NMI | 2);
            vcpu->nmi_pending = false;
            busy = true;
            set_proc_bit(PROC_NMI_WINDOW_EXITING, false);
        } else if (g_vmx.pin & PIN_VIRTUAL_NMIS) {
            set_proc_bit(PROC_NMI_WINDOW_EXITING, true);
        }
    }
    bool open = !busy && guest_interruptible(vcpu);
    if (open && vcpu->pending_irq >= 0 && kvm_accepts_extint(vcpu)) {
        vmwrite(VMCS_ENTRY_INTR_INFO, INTR_INFO_VALID | INTR_TYPE_EXT_INTR | (uint32_t)vcpu->pending_irq);
        vcpu->pending_irq = -1;
        busy = true;
    } else if (open && irqchip) {
        int vec = kvm_lapic_pending(vcpu);
        if (vec >= 0) {
            kvm_lapic_ack(vcpu, vec);
            vmwrite(VMCS_ENTRY_INTR_INFO, INTR_INFO_VALID | INTR_TYPE_EXT_INTR | (uint32_t)vec);
            busy = true;
        }
    }
    bool want = (vcpu->pending_irq >= 0 && kvm_accepts_extint(vcpu)) ||
                (vcpu->run->request_interrupt_window && kvm_accepts_extint(vcpu)) ||
                (irqchip && kvm_lapic_pending(vcpu) >= 0);
    set_proc_bit(PROC_INTR_WINDOW_EXITING, want && (busy || !guest_interruptible(vcpu)));
}

static void save_idt_vectoring(kvm_vcpu_t *vcpu) {
    (void)vcpu;
    uint32_t info = (uint32_t)vmread(VMCS_IDT_VECTORING_INFO);
    if (!(info & INTR_INFO_VALID)) return;
    uint32_t type = info & INTR_INFO_TYPE_MASK;
    vmwrite(VMCS_ENTRY_INTR_INFO, info & ~INTR_INFO_NMI_UNBLOCK_IRET);
    if (info & INTR_INFO_DELIVER_CODE)
        vmwrite(VMCS_ENTRY_EXCEPTION_ERRCODE, vmread(VMCS_IDT_VECTORING_ERRCODE));
    if (type == INTR_TYPE_SW_INTR || type == INTR_TYPE_SW_EXCEPTION || type == INTR_TYPE_PRIV_SW_EXCEPTION)
        vmwrite(VMCS_ENTRY_INSTR_LEN, vmread(VMCS_EXIT_INSTR_LEN));
    if (type == INTR_TYPE_NMI) {
        uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
        vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr & ~INTERRUPTIBILITY_NMI);
    }
}

static void fill_run_state(kvm_vcpu_t *vcpu) {
    struct kvm_run *run = vcpu->run;
    run->if_flag = (vmread(VMCS_GUEST_RFLAGS) & RFLAGS_IF) ? 1 : 0;
    run->cr8 = kvm_vcpu_cr8(vcpu);
    run->apic_base = vcpu->apic_base;
    run->ready_for_interrupt_injection = ready_for_injection(vcpu) ? 1 : 0;
    run->flags = 0;
}

static int vmx_get_msr(kvm_vcpu_t *vcpu, uint32_t msr, uint64_t *data, bool host) {
    switch (msr) {
    case MSR_IA32_SYSENTER_CS: *data = vmread(VMCS_GUEST_SYSENTER_CS); return 0;
    case MSR_IA32_SYSENTER_ESP: *data = vmread(VMCS_GUEST_SYSENTER_ESP); return 0;
    case MSR_IA32_SYSENTER_EIP: *data = vmread(VMCS_GUEST_SYSENTER_EIP); return 0;
    case MSR_IA32_DEBUGCTL: *data = vmread(VMCS_GUEST_DEBUGCTL); return 0;
    case MSR_IA32_PAT: *data = vmread(VMCS_GUEST_PAT); return 0;
    case MSR_EFER: *data = vcpu->efer; return 0;
    case MSR_STAR: *data = vcpu->msr_star; return 0;
    case MSR_LSTAR: *data = vcpu->msr_lstar; return 0;
    case MSR_CSTAR: *data = vcpu->msr_cstar; return 0;
    case MSR_SFMASK: *data = vcpu->msr_sfmask; return 0;
    case MSR_FS_BASE: *data = vmread(VMCS_GUEST_FS_BASE); return 0;
    case MSR_GS_BASE: *data = vmread(VMCS_GUEST_GS_BASE); return 0;
    case MSR_KERNEL_GS_BASE:
        *data = vcpu->guest_loaded ? vmx_rdmsr(MSR_KERNEL_GS_BASE) : vcpu->msr_kernel_gs_base;
        return 0;
    case MSR_TSC_AUX:
        if (!(g_vmx.proc2 & PROC2_ENABLE_RDTSCP) && !host) return -1;
        *data = vcpu->msr_tsc_aux;
        return 0;
    default:
        return kvm_msr_get_common(vcpu, msr, data, host);
    }
}

static int vmx_set_msr(kvm_vcpu_t *vcpu, uint32_t msr, uint64_t data, bool host) {
    switch (msr) {
    case MSR_IA32_SYSENTER_CS: vmwrite(VMCS_GUEST_SYSENTER_CS, data); return 0;
    case MSR_IA32_SYSENTER_ESP:
        if (!kvm_canonical(data)) return -1;
        vmwrite(VMCS_GUEST_SYSENTER_ESP, data);
        return 0;
    case MSR_IA32_SYSENTER_EIP:
        if (!kvm_canonical(data)) return -1;
        vmwrite(VMCS_GUEST_SYSENTER_EIP, data);
        return 0;
    case MSR_IA32_DEBUGCTL:
        if (data & ~3ull) return -1;
        vmwrite(VMCS_GUEST_DEBUGCTL, 0);
        return 0;
    case MSR_IA32_PAT: vmwrite(VMCS_GUEST_PAT, data); return 0;
    case MSR_EFER: {
        uint64_t allowed = EFER_SCE | EFER_LME | EFER_LMA | EFER_NXE;
        if (data & ~allowed) return -1;
        if (!host && ((data ^ vcpu->efer) & EFER_LME) && (vcpu->cr0 & CR0_PG)) return -1;
        if (host) vcpu->efer = data;
        else vcpu->efer = (data & ~EFER_LMA) | (vcpu->efer & EFER_LMA);
        update_entry_controls(vcpu);
        return 0;
    }
    case MSR_STAR: vcpu->msr_star = data; if (vcpu->guest_loaded) vmx_wrmsr(MSR_STAR, data); return 0;
    case MSR_LSTAR:
        if (!kvm_canonical(data)) return -1;
        vcpu->msr_lstar = data; if (vcpu->guest_loaded) vmx_wrmsr(MSR_LSTAR, data);
        return 0;
    case MSR_CSTAR:
        if (!kvm_canonical(data)) return -1;
        vcpu->msr_cstar = data; if (vcpu->guest_loaded) vmx_wrmsr(MSR_CSTAR, data);
        return 0;
    case MSR_SFMASK: vcpu->msr_sfmask = data; if (vcpu->guest_loaded) vmx_wrmsr(MSR_SFMASK, data); return 0;
    case MSR_FS_BASE:
        if (!kvm_canonical(data)) return -1;
        vmwrite(VMCS_GUEST_FS_BASE, data);
        return 0;
    case MSR_GS_BASE:
        if (!kvm_canonical(data)) return -1;
        vmwrite(VMCS_GUEST_GS_BASE, data);
        return 0;
    case MSR_KERNEL_GS_BASE:
        if (!kvm_canonical(data)) return -1;
        vcpu->msr_kernel_gs_base = data;
        if (vcpu->guest_loaded) vmx_wrmsr(MSR_KERNEL_GS_BASE, data);
        return 0;
    case MSR_TSC_AUX:
        if (!(g_vmx.proc2 & PROC2_ENABLE_RDTSCP) && !host) return -1;
        if (data >> 32) return -1;
        vcpu->msr_tsc_aux = data;
        if (vcpu->guest_loaded && (g_vmx.proc2 & PROC2_ENABLE_RDTSCP)) vmx_wrmsr(MSR_TSC_AUX, data);
        return 0;
    default:
        return kvm_msr_set_common(vcpu, msr, data, host);
    }
}

static int handle_io(kvm_vcpu_t *vcpu) {
    uint64_t q = vmread(VMCS_EXIT_QUALIFICATION);
    uint8_t size = (uint8_t)((q & 7) + 1);
    bool in = (q >> 3) & 1;
    bool string = (q >> 4) & 1;
    bool rep = (q >> 5) & 1;
    uint16_t port = (uint16_t)(q >> 16);
    int addr_size = 0, seg = SEG_DS;
    if (string) {
        uint32_t info = (uint32_t)vmread(VMCS_EXIT_INSTR_INFO);
        addr_size = g_vmx.ins_outs_info ? (int)((info >> 7) & 7) : ((vcpu->efer & EFER_LMA) ? 2 : 1);
        seg = in ? SEG_ES : (g_vmx.ins_outs_info ? (int)((info >> 15) & 7) : SEG_DS);
    }
    uint64_t next = vmread(VMCS_GUEST_RIP) + vmread(VMCS_EXIT_INSTR_LEN);
    return kvm_handle_pio(vcpu, port, size, in, string, rep, addr_size, seg, next);
}

static const uint32_t g_msr_list[] = {
    MSR_IA32_TSC, MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
    MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SFMASK, MSR_KERNEL_GS_BASE, MSR_TSC_AUX,
    MSR_IA32_MISC_ENABLE, MSR_IA32_FEATURE_CONTROL, MSR_IA32_TSC_ADJUST, MSR_IA32_PAT,
    MSR_IA32_MCG_STATUS, MSR_IA32_MCG_CTL, MSR_IA32_TSC_DEADLINE,
};

static uint32_t vmx_msr_index_list(uint32_t *out, uint32_t max) {
    uint32_t n = sizeof g_msr_list / sizeof g_msr_list[0];
    for (uint32_t i = 0; i < n && i < max; i++) out[i] = g_msr_list[i];
    return n;
}

static int handle_cr(kvm_vcpu_t *vcpu) {
    uint64_t q = vmread(VMCS_EXIT_QUALIFICATION);
    int cr = q & 15;
    int type = (q >> 4) & 3;
    int reg = (q >> 8) & 15;
    if (type == 0) {
        uint64_t val = vmx_reg_read(vcpu, reg);
        switch (cr) {
        case 0:
            if (set_cr0_checked(vcpu, val) < 0) { emu_inject_gp(vcpu); return 1; }
            break;
        case 3:
            vmwrite(VMCS_GUEST_CR3, val);
            load_pdptes(vcpu);
            break;
        case 4:
            if (set_cr4_checked(vcpu, val) < 0) { emu_inject_gp(vcpu); return 1; }
            break;
        case 8: {
            uint64_t old = vcpu->cr8;
            if (val & ~0xFull) { emu_inject_gp(vcpu); return 1; }
            if (vcpu->vm->irqchip_split) {
                kvm_lapic_set_cr8(vcpu, (uint8_t)val);
                vcpu->cr8 = val;
                skip_instruction(vcpu);
                return 1;
            }
            vcpu->cr8 = val;
            skip_instruction(vcpu);
            if (val < old) {
                vcpu->run->exit_reason = KVM_EXIT_SET_TPR;
                return 0;
            }
            return 1;
        }
        default:
            emu_inject_ud(vcpu);
            return 1;
        }
    } else if (type == 1) {
        if (cr == 3) vmx_reg_write(vcpu, reg, vmread(VMCS_GUEST_CR3));
        else if (cr == 8) vmx_reg_write(vcpu, reg, kvm_vcpu_cr8(vcpu));
        else { emu_inject_ud(vcpu); return 1; }
    } else if (type == 2) {
        apply_cr0(vcpu, guest_cr0(vcpu) & ~CR0_TS);
    } else {
        uint64_t msw = (q >> 16) & 0xFFFF;
        uint64_t cr0 = guest_cr0(vcpu);
        uint64_t n = (cr0 & ~0xEull) | (msw & 0xF) | (cr0 & CR0_PE);
        if (set_cr0_checked(vcpu, n) < 0) { emu_inject_gp(vcpu); return 1; }
    }
    skip_instruction(vcpu);
    return 1;
}

static int handle_dr(kvm_vcpu_t *vcpu) {
    uint64_t q = vmread(VMCS_EXIT_QUALIFICATION);
    int dr = q & 7;
    bool from = (q >> 4) & 1;
    int reg = (q >> 8) & 15;
    if ((dr == 4 || dr == 5) && (vcpu->cr4 & CR4_DE)) { emu_inject_ud(vcpu); return 1; }
    if (dr == 4) dr = 6;
    if (dr == 5) dr = 7;
    if (from) {
        uint64_t v = dr < 4 ? vcpu->dr[dr] : dr == 6 ? vcpu->dr6 : vmread(VMCS_GUEST_DR7);
        vmx_reg_write(vcpu, reg, v);
    } else {
        uint64_t v = vmx_reg_read(vcpu, reg);
        if (dr < 4) vcpu->dr[dr] = v;
        else if (dr == 6) vcpu->dr6 = (v & 0xFFFFull) | 0xFFFF0FF0ull;
        else vmwrite(VMCS_GUEST_DR7, (v & 0xFFFFFFFFull) | 0x400);
    }
    skip_instruction(vcpu);
    return 1;
}

static int handle_ept_violation(kvm_vcpu_t *vcpu) {
    uint64_t gpa = vmread(VMCS_GUEST_PHYS_ADDR);
    uint64_t q = vmread(VMCS_EXIT_QUALIFICATION);
    if ((q & EPT_VIOL_NMI_UNBLOCK) && !(vmread(VMCS_IDT_VECTORING_INFO) & INTR_INFO_VALID)) {
        uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
        vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr | INTERRUPTIBILITY_NMI);
    }
    int r = ept_fault(vcpu, gpa, q);
    if (r == 0) return 1;
    if (r < 0 && r != -EFAULT) return kvm_exit_internal(vcpu, KVM_INTERNAL_ERROR_EMULATION, gpa, q);
    return emu_mmio(vcpu, gpa);
}

static void dump_guest(kvm_vcpu_t *vcpu, uint32_t reason) {
    serial_printf("[kvm] vcpu%u exit %u qual=%llx rip=%llx cs=%llx/%llx ar=%llx cr0=%llx/%llx cr4=%llx efer=%llx err=%llu\n",
                  vcpu->id, reason, vmread(VMCS_EXIT_QUALIFICATION), vmread(VMCS_GUEST_RIP),
                  vmread(VMCS_GUEST_CS_SEL), vmread(VMCS_GUEST_CS_BASE), vmread(VMCS_GUEST_CS_AR),
                  vmread(VMCS_GUEST_CR0), vcpu->cr0, vmread(VMCS_GUEST_CR4), vcpu->efer,
                  vmread(VMCS_INSTR_ERROR));
}

static int handle_exit(kvm_vcpu_t *vcpu, uint32_t reason) {
    struct kvm_run *run = vcpu->run;
    switch (reason & 0xFFFF) {
    case EXIT_REASON_EXCEPTION_NMI:
    case EXIT_REASON_EXTERNAL_INT:
        return 1;
    case EXIT_REASON_TRIPLE_FAULT:
        run->exit_reason = KVM_EXIT_SHUTDOWN;
        return 0;
    case EXIT_REASON_INIT:
    case EXIT_REASON_SIPI:
        return 1;
    case EXIT_REASON_INTR_WINDOW:
        set_proc_bit(PROC_INTR_WINDOW_EXITING, false);
        if (run->request_interrupt_window && ready_for_injection(vcpu)) {
            run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
            return 0;
        }
        return 1;
    case EXIT_REASON_NMI_WINDOW:
        set_proc_bit(PROC_NMI_WINDOW_EXITING, false);
        return 1;
    case EXIT_REASON_CPUID:
        return kvm_handle_cpuid(vcpu);
    case EXIT_REASON_HLT:
        skip_instruction(vcpu);
        if (vcpu->vm->irqchip_split) return kvm_vcpu_halt(vcpu);
        if (vcpu->pending_irq >= 0 && guest_interruptible(vcpu)) return 1;
        run->exit_reason = KVM_EXIT_HLT;
        return 0;
    case EXIT_REASON_INVD:
    case EXIT_REASON_WBINVD:
    case EXIT_REASON_MWAIT:
    case EXIT_REASON_MONITOR:
    case EXIT_REASON_PAUSE:
    case EXIT_REASON_INVLPG:
        skip_instruction(vcpu);
        return 1;
    case EXIT_REASON_VMCALL:
        vcpu->regs.gpr[VCPU_REG_RAX] = (uint64_t)-1000;
        skip_instruction(vcpu);
        return 1;
    case EXIT_REASON_XSETBV:
        return kvm_handle_xsetbv(vcpu);
    case EXIT_REASON_RDPMC:
        emu_inject_gp(vcpu);
        return 1;
    case EXIT_REASON_GETSEC:
    case EXIT_REASON_VMCLEAR: case EXIT_REASON_VMLAUNCH: case EXIT_REASON_VMPTRLD:
    case EXIT_REASON_VMPTRST: case EXIT_REASON_VMREAD: case EXIT_REASON_VMRESUME:
    case EXIT_REASON_VMWRITE: case EXIT_REASON_VMXOFF: case EXIT_REASON_VMXON:
    case EXIT_REASON_INVEPT: case EXIT_REASON_INVVPID: case EXIT_REASON_VMFUNC:
    case EXIT_REASON_RDRAND: case EXIT_REASON_RDSEED: case EXIT_REASON_INVPCID:
    case EXIT_REASON_XSAVES: case EXIT_REASON_XRSTORS: case EXIT_REASON_RSM:
        emu_inject_ud(vcpu);
        return 1;
    case EXIT_REASON_CR_ACCESS:
        return handle_cr(vcpu);
    case EXIT_REASON_DR_ACCESS:
        return handle_dr(vcpu);
    case EXIT_REASON_IO:
        return handle_io(vcpu);
    case EXIT_REASON_RDMSR:
        return kvm_handle_rdmsr(vcpu);
    case EXIT_REASON_WRMSR:
        return kvm_handle_wrmsr(vcpu);
    case EXIT_REASON_EPT_VIOLATION:
        return handle_ept_violation(vcpu);
    case EXIT_REASON_PREEMPT_TIMER:
        return 1;
    case EXIT_REASON_EPT_MISCONFIG:
        dump_guest(vcpu, reason);
        return kvm_exit_internal(vcpu, KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON, reason, vmread(VMCS_GUEST_PHYS_ADDR));
    default:
        dump_guest(vcpu, reason);
        return kvm_exit_internal(vcpu, KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON, reason, vmread(VMCS_EXIT_QUALIFICATION));
    }
}

static void arm_preemption_timer(kvm_vcpu_t *vcpu) {
    if (!g_vmx.preempt) return;
    uint32_t pin = g_vmx.pin;
    uint64_t dl = vcpu->vm->irqchip_split ? kvm_lapic_timer_deadline(vcpu) : 0;
    if (dl) {
        uint64_t now = sched_now_ns();
        uint64_t ns = dl > now ? dl - now : 0;
        if (ns > 1000000000000ull) ns = 1000000000000ull;
        uint64_t ticks = (ns * g_tsc_khz / 1000000ull) >> g_vmx.preempt_shift;
        if (ticks > 0xFFFFFFFFull) ticks = 0xFFFFFFFFull;
        vmwrite(VMCS_PREEMPTION_TIMER, ticks);
        pin |= PIN_PREEMPTION_TIMER;
    }
    if ((uint32_t)vmread(VMCS_PIN_CONTROLS) != pin) vmwrite(VMCS_PIN_CONTROLS, pin);
}

static inline void xsetbv0(uint64_t v) {
    asm volatile("xsetbv" :: "c"(0), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)) : "memory");
}

static int vmx_vcpu_run(kvm_vcpu_t *vcpu) {
    task_t *me = (task_t *)percpu_current_task();
    struct kvm_run *run = vcpu->run;
    int r = 1;

    vmcs_claim(vcpu);
    vcpu->owner = me;
    me->kvm_vcpu = vcpu;
    vmcs_load(vcpu);
    asm volatile("sti" ::: "memory");

    run->exit_reason = KVM_EXIT_UNKNOWN;
    r = kvm_complete_pending(vcpu);
    if (r > 0 && vcpu->vm->irqchip_split &&
        __atomic_load_n(&vcpu->mp_state, __ATOMIC_SEQ_CST) == KVM_MP_STATE_HALTED)
        r = kvm_vcpu_halt(vcpu);

    while (r > 0) {
        if (kvm_should_stop(vcpu)) {
            run->exit_reason = KVM_EXIT_INTR;
            r = -EINTR;
            break;
        }
        if (vcpu->vm->irqchip_split) {
            uint32_t mp = __atomic_load_n(&vcpu->mp_state, __ATOMIC_SEQ_CST);
            if (mp != KVM_MP_STATE_RUNNABLE && mp != KVM_MP_STATE_HALTED) {
                r = kvm_vcpu_mp_wait(vcpu);
                continue;
            }
            kvm_lapic_timer_check(vcpu, sched_now_ns());
        }
        kvm_clock_sync(vcpu);
        asm volatile("cli" ::: "memory");
        percpu_t *pc = get_percpu();
        if (pc->need_resched) {
            pc->need_resched = false;
            sched_reschedule();
            asm volatile("sti" ::: "memory");
            continue;
        }
        vcpu_prepare_entry(vcpu);
        if (run->request_interrupt_window && ready_for_injection(vcpu)) {
            asm volatile("sti" ::: "memory");
            run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
            r = 0;
            break;
        }
        __atomic_and_fetch(&vcpu->requests, ~VCPU_REQ_EVENT, __ATOMIC_ACQ_REL);
        inject_events(vcpu);
        if (run->exit_reason == KVM_EXIT_SHUTDOWN) {
            asm volatile("sti" ::: "memory");
            r = 0;
            break;
        }
        arm_preemption_timer(vcpu);
        vcpu->mode = VCPU_IN_GUEST;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        uint32_t req = __atomic_exchange_n(&vcpu->requests, 0, __ATOMIC_ACQ_REL);
        if ((req & VCPU_REQ_TLB_FLUSH) || vcpu->tlb_gen_seen != vcpu->vm->tlb_gen) {
            vcpu->tlb_gen_seen = vcpu->vm->tlb_gen;
            vmx_flush_ept(vcpu);
        }
        if (req & VCPU_REQ_EVENT) {
            vcpu->mode = VCPU_OUTSIDE_GUEST;
            asm volatile("sti" ::: "memory");
            continue;
        }
        bool xsw = fpu_xcr0 && (vcpu->cr4 & CR4_OSXSAVE) && vcpu->xcr0 != fpu_xcr0;
        if (xsw) xsetbv0(vcpu->xcr0);
        int er = vmx_enter(&vcpu->regs, vcpu->launched ? 1 : 0);
        if (xsw) xsetbv0(fpu_xcr0);
        vcpu->mode = VCPU_OUTSIDE_GUEST;
        if (er) {
            uint64_t ierr = vmread(VMCS_INSTR_ERROR);
            dump_guest(vcpu, 0xFFFFFFFFu);
            asm volatile("sti" ::: "memory");
            run->exit_reason = KVM_EXIT_FAIL_ENTRY;
            run->fail_entry.hardware_entry_failure_reason = ierr | ((uint64_t)er << 32);
            run->fail_entry.cpu = smp_cpu_index();
            r = 0;
            break;
        }
        vcpu->launched = true;
        uint32_t reason = (uint32_t)vmread(VMCS_EXIT_REASON);
        if ((reason & 0xFFFF) == EXIT_REASON_EXCEPTION_NMI) {
            uint32_t ei = (uint32_t)vmread(VMCS_EXIT_INTR_INFO);
            if ((ei & INTR_INFO_TYPE_MASK) == INTR_TYPE_NMI) asm volatile("int $2");
        }
        if (reason & EXIT_REASON_FAILED_ENTRY) {
            dump_guest(vcpu, reason);
            vcpu->launched = false;
            asm volatile("sti" ::: "memory");
            run->exit_reason = KVM_EXIT_FAIL_ENTRY;
            run->fail_entry.hardware_entry_failure_reason = reason & 0xFFFF;
            run->fail_entry.cpu = smp_cpu_index();
            r = 0;
            break;
        }
        vmwrite(VMCS_ENTRY_INTR_INFO, 0);
        save_idt_vectoring(vcpu);
        asm volatile("sti" ::: "memory");
        r = handle_exit(vcpu, reason);
        if (run->exit_reason == KVM_EXIT_SHUTDOWN) r = 0;
    }

    asm volatile("cli" ::: "memory");
    vmcs_load(vcpu);
    fill_run_state(vcpu);
    guest_state_put(vcpu);
    me->kvm_vcpu = NULL;
    asm volatile("sti" ::: "memory");
    return r < 0 ? r : 0;
}

static void vmx_get_regs(kvm_vcpu_t *vcpu, struct kvm_regs *r) {
    vmx_vmcs_enter(vcpu);
    r->rax = vcpu->regs.gpr[VCPU_REG_RAX]; r->rbx = vcpu->regs.gpr[VCPU_REG_RBX];
    r->rcx = vcpu->regs.gpr[VCPU_REG_RCX]; r->rdx = vcpu->regs.gpr[VCPU_REG_RDX];
    r->rsi = vcpu->regs.gpr[VCPU_REG_RSI]; r->rdi = vcpu->regs.gpr[VCPU_REG_RDI];
    r->rbp = vcpu->regs.gpr[VCPU_REG_RBP];
    r->r8 = vcpu->regs.gpr[8];   r->r9 = vcpu->regs.gpr[9];
    r->r10 = vcpu->regs.gpr[10]; r->r11 = vcpu->regs.gpr[11];
    r->r12 = vcpu->regs.gpr[12]; r->r13 = vcpu->regs.gpr[13];
    r->r14 = vcpu->regs.gpr[14]; r->r15 = vcpu->regs.gpr[15];
    r->rsp = vmread(VMCS_GUEST_RSP);
    r->rip = vmread(VMCS_GUEST_RIP);
    r->rflags = vmread(VMCS_GUEST_RFLAGS);
    vmx_vmcs_leave(vcpu);
}

static void vmx_set_regs(kvm_vcpu_t *vcpu, const struct kvm_regs *r) {
    vmx_vmcs_enter(vcpu);
    vcpu->regs.gpr[VCPU_REG_RAX] = r->rax; vcpu->regs.gpr[VCPU_REG_RBX] = r->rbx;
    vcpu->regs.gpr[VCPU_REG_RCX] = r->rcx; vcpu->regs.gpr[VCPU_REG_RDX] = r->rdx;
    vcpu->regs.gpr[VCPU_REG_RSI] = r->rsi; vcpu->regs.gpr[VCPU_REG_RDI] = r->rdi;
    vcpu->regs.gpr[VCPU_REG_RBP] = r->rbp;
    vcpu->regs.gpr[8] = r->r8;   vcpu->regs.gpr[9] = r->r9;
    vcpu->regs.gpr[10] = r->r10; vcpu->regs.gpr[11] = r->r11;
    vcpu->regs.gpr[12] = r->r12; vcpu->regs.gpr[13] = r->r13;
    vcpu->regs.gpr[14] = r->r14; vcpu->regs.gpr[15] = r->r15;
    vmwrite(VMCS_GUEST_RSP, r->rsp);
    vmwrite(VMCS_GUEST_RIP, r->rip);
    vmwrite(VMCS_GUEST_RFLAGS, (r->rflags & 0x3F7FD7ull) | 2);
    vcpu->pend.active = false;
    vmx_vmcs_leave(vcpu);
}

static void vmx_get_sregs(kvm_vcpu_t *vcpu, struct kvm_sregs *s) {
    memset(s, 0, sizeof *s);
    vmx_vmcs_enter(vcpu);
    read_seg(SEG_CS, &s->cs);
    read_seg(SEG_DS, &s->ds);
    read_seg(SEG_ES, &s->es);
    read_seg(SEG_FS, &s->fs);
    read_seg(SEG_GS, &s->gs);
    read_seg(SEG_SS, &s->ss);
    read_seg(SEG_TR, &s->tr);
    read_seg(SEG_LDTR, &s->ldt);
    s->gdt.base = vmread(VMCS_GUEST_GDTR_BASE);
    s->gdt.limit = (uint16_t)vmread(VMCS_GUEST_GDTR_LIMIT);
    s->idt.base = vmread(VMCS_GUEST_IDTR_BASE);
    s->idt.limit = (uint16_t)vmread(VMCS_GUEST_IDTR_LIMIT);
    s->cr0 = guest_cr0(vcpu);
    s->cr2 = vcpu->regs.cr2;
    s->cr3 = vmread(VMCS_GUEST_CR3);
    s->cr4 = guest_cr4(vcpu);
    s->cr8 = kvm_vcpu_cr8(vcpu);
    s->efer = vcpu->efer;
    s->apic_base = vcpu->apic_base;
    uint32_t info = (uint32_t)vmread(VMCS_ENTRY_INTR_INFO);
    if (vcpu->pending_irq >= 0) {
        uint32_t v = (uint32_t)vcpu->pending_irq;
        s->interrupt_bitmap[v / 64] |= 1ull << (v % 64);
    } else if ((info & INTR_INFO_VALID) && (info & INTR_INFO_TYPE_MASK) == INTR_TYPE_EXT_INTR) {
        uint32_t v = info & 0xFF;
        s->interrupt_bitmap[v / 64] |= 1ull << (v % 64);
    }
    vmx_vmcs_leave(vcpu);
}

static int vmx_set_sregs(kvm_vcpu_t *vcpu, const struct kvm_sregs *s) {
    if (s->cr4 & ~vmx_supported_cr4()) return -EINVAL;
    vmx_vmcs_enter(vcpu);
    write_seg(SEG_CS, &s->cs);
    write_seg(SEG_DS, &s->ds);
    write_seg(SEG_ES, &s->es);
    write_seg(SEG_FS, &s->fs);
    write_seg(SEG_GS, &s->gs);
    write_seg(SEG_SS, &s->ss);
    struct kvm_segment tr = s->tr;
    if (!tr.present || tr.unusable) { tr.present = 1; tr.unusable = 0; tr.type = 0xB; tr.s = 0; }
    if ((tr.type & 0xF) != 3 && (tr.type & 0xF) != 0xB) tr.type = 0xB;
    write_seg(SEG_TR, &tr);
    write_seg(SEG_LDTR, &s->ldt);
    vmwrite(VMCS_GUEST_GDTR_BASE, s->gdt.base);
    vmwrite(VMCS_GUEST_GDTR_LIMIT, s->gdt.limit);
    vmwrite(VMCS_GUEST_IDTR_BASE, s->idt.base);
    vmwrite(VMCS_GUEST_IDTR_LIMIT, s->idt.limit);
    vcpu->regs.cr2 = s->cr2;
    vmwrite(VMCS_GUEST_CR3, s->cr3);
    vcpu->cr8 = s->cr8;
    vcpu->apic_base = s->apic_base;
    if (vcpu->vm->irqchip_split) kvm_lapic_set_cr8(vcpu, (uint8_t)(s->cr8 & 0xF));
    vcpu->efer = s->efer;
    apply_cr4(vcpu, s->cr4);
    vcpu->cr0 = s->cr0;
    apply_cr0(vcpu, s->cr0);
    if ((s->cr0 & CR0_PG) && (s->efer & EFER_LME)) vcpu->efer |= EFER_LMA;
    else vcpu->efer &= ~EFER_LMA;
    update_entry_controls(vcpu);
    load_pdptes(vcpu);
    for (int i = 0; i < 4; i++) {
        if (!s->interrupt_bitmap[i]) continue;
        vcpu->pending_irq = i * 64 + __builtin_ctzll(s->interrupt_bitmap[i]);
        break;
    }
    vmx_vmcs_leave(vcpu);
    return 0;
}

static void vmx_get_events(kvm_vcpu_t *vcpu, struct kvm_vcpu_events *e) {
    memset(e, 0, sizeof *e);
    vmx_vmcs_enter(vcpu);
    uint32_t info = (uint32_t)vmread(VMCS_ENTRY_INTR_INFO);
    if (info & INTR_INFO_VALID) {
        uint32_t type = info & INTR_INFO_TYPE_MASK;
        if (type == INTR_TYPE_HW_EXCEPTION || type == INTR_TYPE_SW_EXCEPTION) {
            e->exception.injected = 1;
            e->exception.nr = info & 0xFF;
            e->exception.has_error_code = (info & INTR_INFO_DELIVER_CODE) ? 1 : 0;
            e->exception.error_code = (uint32_t)vmread(VMCS_ENTRY_EXCEPTION_ERRCODE);
        } else if (type == INTR_TYPE_EXT_INTR || type == INTR_TYPE_SW_INTR) {
            e->interrupt.injected = 1;
            e->interrupt.nr = info & 0xFF;
            e->interrupt.soft = type == INTR_TYPE_SW_INTR;
        } else if (type == INTR_TYPE_NMI) {
            e->nmi.injected = 1;
        }
    }
    if (vcpu->exc_pending) {
        e->exception.injected = 1;
        e->exception.nr = vcpu->exc_vector;
        e->exception.has_error_code = vcpu->exc_has_error;
        e->exception.error_code = vcpu->exc_error;
    }
    if (vcpu->pending_irq >= 0) {
        e->interrupt.injected = 1;
        e->interrupt.nr = (uint8_t)vcpu->pending_irq;
    }
    uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
    if (intr & INTERRUPTIBILITY_STI) e->interrupt.shadow |= KVM_X86_SHADOW_INT_STI;
    if (intr & INTERRUPTIBILITY_MOV_SS) e->interrupt.shadow |= KVM_X86_SHADOW_INT_MOV_SS;
    e->nmi.pending = vcpu->nmi_pending;
    e->nmi.masked = (intr & INTERRUPTIBILITY_NMI) ? 1 : 0;
    e->sipi_vector = vcpu->sipi_vector;
    e->flags = KVM_VCPUEVENT_VALID_NMI_PENDING | KVM_VCPUEVENT_VALID_SIPI_VECTOR | KVM_VCPUEVENT_VALID_SHADOW;
    vmx_vmcs_leave(vcpu);
}

static int vmx_set_events(kvm_vcpu_t *vcpu, const struct kvm_vcpu_events *e) {
    vmx_vmcs_enter(vcpu);
    vmwrite(VMCS_ENTRY_INTR_INFO, 0);
    vcpu->exc_pending = false;
    if (e->exception.injected || e->exception.pending) {
        vcpu->exc_pending = true;
        vcpu->exc_vector = e->exception.nr;
        vcpu->exc_has_error = e->exception.has_error_code;
        vcpu->exc_error = e->exception.error_code;
    }
    vcpu->pending_irq = e->interrupt.injected ? e->interrupt.nr : -1;
    uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
    if (e->flags & KVM_VCPUEVENT_VALID_SHADOW) {
        intr &= ~(INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS);
        if (e->interrupt.shadow & KVM_X86_SHADOW_INT_STI) intr |= INTERRUPTIBILITY_STI;
        if (e->interrupt.shadow & KVM_X86_SHADOW_INT_MOV_SS) intr |= INTERRUPTIBILITY_MOV_SS;
    }
    if (e->nmi.masked) intr |= INTERRUPTIBILITY_NMI;
    else intr &= ~INTERRUPTIBILITY_NMI;
    vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr);
    if (e->flags & KVM_VCPUEVENT_VALID_NMI_PENDING) vcpu->nmi_pending = e->nmi.pending;
    if (e->nmi.injected) vcpu->nmi_pending = true;
    if (e->flags & KVM_VCPUEVENT_VALID_SIPI_VECTOR) vcpu->sipi_vector = e->sipi_vector;
    vmx_vmcs_leave(vcpu);
    return 0;
}

static uint64_t vmx_get_rip(kvm_vcpu_t *vcpu) { (void)vcpu; return vmread(VMCS_GUEST_RIP); }
static void vmx_set_rip(kvm_vcpu_t *vcpu, uint64_t rip) { (void)vcpu; vmwrite(VMCS_GUEST_RIP, rip); }
static uint64_t vmx_get_rflags(kvm_vcpu_t *vcpu) { (void)vcpu; return vmread(VMCS_GUEST_RFLAGS); }
static void vmx_set_rflags(kvm_vcpu_t *vcpu, uint64_t fl) { (void)vcpu; vmwrite(VMCS_GUEST_RFLAGS, fl); }
static uint64_t vmx_get_cr3(kvm_vcpu_t *vcpu) { (void)vcpu; return vmread(VMCS_GUEST_CR3); }
static uint64_t vmx_get_dr7(kvm_vcpu_t *vcpu) { (void)vcpu; return vmread(VMCS_GUEST_DR7); }
static void vmx_set_dr7(kvm_vcpu_t *vcpu, uint64_t v) { (void)vcpu; vmwrite(VMCS_GUEST_DR7, (v & 0xFFFFFFFFull) | 0x400); }
static void vmx_apply_tsc_offset(kvm_vcpu_t *vcpu) { vmwrite(VMCS_TSC_OFFSET, (uint64_t)vcpu->tsc_offset); }

static uint64_t vmx_seg_base(kvm_vcpu_t *vcpu, int seg) {
    (void)vcpu;
    if (seg < 0 || seg > SEG_GS) return 0;
    return vmread(g_seg_fields[seg].base);
}

static uint32_t vmx_cs_mode(kvm_vcpu_t *vcpu) {
    (void)vcpu;
    uint64_t ar = vmread(VMCS_GUEST_CS_AR);
    return (((ar >> 13) & 1) ? KVM_CS_LONG : 0) | (((ar >> 14) & 1) ? KVM_CS_DB : 0);
}

static int vmx_cpl(kvm_vcpu_t *vcpu) {
    if (!(vcpu->cr0 & CR0_PE)) return 0;
    return (int)((vmread(VMCS_GUEST_SS_AR) >> 5) & 3);
}

static uint64_t vmx_get_pdpte(kvm_vcpu_t *vcpu, int i) {
    (void)vcpu;
    static const uint32_t f[4] = { VMCS_GUEST_PDPTE0, VMCS_GUEST_PDPTE1, VMCS_GUEST_PDPTE2, VMCS_GUEST_PDPTE3 };
    return vmread(f[i & 3]);
}

static void vmx_clear_int_shadow(kvm_vcpu_t *vcpu) {
    (void)vcpu;
    uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
    if (intr & (INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS))
        vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr & ~(INTERRUPTIBILITY_STI | INTERRUPTIBILITY_MOV_SS));
}

static void vmx_init_reset(kvm_vcpu_t *vcpu) {
    vmwrite(VMCS_ENTRY_INTR_INFO, 0);
    vmcs_reset_guest(vcpu);
}

static void vmx_sipi(kvm_vcpu_t *vcpu, uint8_t vec) {
    (void)vcpu;
    struct kvm_segment cs = { .base = (uint64_t)vec << 12, .limit = 0xFFFF,
                              .selector = (uint16_t)(vec << 8), .type = 0xB, .present = 1, .s = 1 };
    write_seg(SEG_CS, &cs);
    vmwrite(VMCS_GUEST_RIP, 0);
}

static void vmx_adjust_cpuid(struct kvm_cpuid_entry2 *e) {
    if (e->function == 7 && e->index == 0 && !(g_vmx.proc2 & PROC2_ENABLE_INVPCID)) e->ebx &= ~(1u << 10);
    if (e->function == 0x80000001u && !(g_vmx.proc2 & PROC2_ENABLE_RDTSCP)) e->edx &= ~(1u << 27);
}

const kvm_arch_ops_t vmx_ops = {
    .name = "VMX",
    .vcpu_create = vmx_vcpu_create,
    .vcpu_destroy = vmx_vcpu_destroy,
    .vcpu_run = vmx_vcpu_run,
    .sched_out = vmx_sched_out,
    .sched_in = vmx_sched_in,
    .state_enter = vmx_vmcs_enter,
    .state_leave = vmx_vmcs_leave,
    .kick = vmx_kick,
    .get_regs = vmx_get_regs,
    .set_regs = vmx_set_regs,
    .get_sregs = vmx_get_sregs,
    .set_sregs = vmx_set_sregs,
    .get_events = vmx_get_events,
    .set_events = vmx_set_events,
    .get_msr = vmx_get_msr,
    .set_msr = vmx_set_msr,
    .get_dr7 = vmx_get_dr7,
    .set_dr7 = vmx_set_dr7,
    .supported_cr4 = vmx_supported_cr4,
    .adjust_cpuid = vmx_adjust_cpuid,
    .msr_index_list = vmx_msr_index_list,
    .apply_tsc_offset = vmx_apply_tsc_offset,
    .get_rip = vmx_get_rip,
    .set_rip = vmx_set_rip,
    .get_rflags = vmx_get_rflags,
    .set_rflags = vmx_set_rflags,
    .get_reg = vmx_reg_read,
    .set_reg = vmx_reg_write,
    .seg_base = vmx_seg_base,
    .cs_mode = vmx_cs_mode,
    .cpl = vmx_cpl,
    .get_cr3 = vmx_get_cr3,
    .get_pdpte = vmx_get_pdpte,
    .clear_int_shadow = vmx_clear_int_shadow,
    .skip_insn = skip_instruction,
    .init_reset = vmx_init_reset,
    .sipi = vmx_sipi,
};
