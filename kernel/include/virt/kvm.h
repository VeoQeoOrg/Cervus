#ifndef VIRT_KVM_H
#define VIRT_KVM_H

#include <stdint.h>
#include <stdbool.h>
#include "kvm_abi.h"
#include "../sched/kmutex.h"
#include "../sched/spinlock.h"

struct mm;
struct vnode;
struct task;

#define KVM_MAX_VCPUS        64
#define KVM_MAX_MEMSLOTS     509
#define KVM_MAX_CPUID        128
#define KVM_MAX_IOEVENTFDS   1000
#define KVM_MCE_BANKS        10
#define KVM_RUN_PAGES        2
#define KVM_PIO_PAGE_OFFSET  1

enum {
    VCPU_REG_RAX, VCPU_REG_RCX, VCPU_REG_RDX, VCPU_REG_RBX,
    VCPU_REG_RSP, VCPU_REG_RBP, VCPU_REG_RSI, VCPU_REG_RDI,
    VCPU_REG_R8, VCPU_REG_R9, VCPU_REG_R10, VCPU_REG_R11,
    VCPU_REG_R12, VCPU_REG_R13, VCPU_REG_R14, VCPU_REG_R15,
    VCPU_NR_GPRS
};

typedef struct {
    uint64_t gpr[VCPU_NR_GPRS];
    uint64_t cr2;
} vmx_regs_t;

_Static_assert(__builtin_offsetof(vmx_regs_t, cr2) == 128, "vmx_regs_t layout is used by vmx_entry.asm");

typedef struct kvm_memslot {
    bool      used;
    uint32_t  id;
    uint32_t  flags;
    uint64_t  gfn;
    uint64_t  npages;
    uint64_t  hva;
    uint64_t *dirty;
} kvm_memslot_t;

typedef struct kvm_ioeventfd_entry {
    struct kvm_ioeventfd_entry *next;
    uint64_t       addr;
    uint32_t       len;
    bool           pio;
    bool           datamatch;
    uint64_t       data;
    struct vnode  *efd;
} kvm_ioeventfd_entry_t;

typedef struct kvm_vm kvm_vm_t;
typedef struct kvm_vcpu kvm_vcpu_t;

enum { VCPU_OUTSIDE_GUEST = 0, VCPU_IN_GUEST = 1 };

#define VCPU_REQ_TLB_FLUSH (1u << 0)

enum { PEND_NONE = 0, PEND_PIO = 1, PEND_MMIO = 2 };

typedef struct {
    bool     active;
    uint8_t  kind;
    uint8_t  phase;
    bool     is_write;
    bool     string;
    bool     rep;
    uint8_t  size;
    uint8_t  dst_reg;
    uint8_t  dst_size;
    bool     sign_extend;
    bool     high_byte;
    uint8_t  op;
    uint64_t gpa;
    uint64_t rmw_operand;
    uint64_t next_rip;
    uint32_t pio_count;
    uint16_t pio_port;
    bool     pio_in;
    uint64_t pio_gva;
    int      pio_seg;
    uint8_t  addr_size;
} kvm_pending_t;

struct kvm_vcpu {
    kvm_vm_t        *vm;
    uint32_t         id;
    kmutex_t         mutex;
    volatile int     refcount;

    void            *vmcs;
    uint64_t         vmcs_phys;
    bool             launched;
    int              loaded_cpu;
    int              last_cpu;
    uint16_t         vpid;
    uint64_t         tlb_gen_seen;
    volatile int     mode;
    volatile uint32_t requests;
    struct task     *owner;

    struct kvm_run  *run;
    uint64_t         run_phys;

    vmx_regs_t       regs;
    uint64_t         cr0;
    uint64_t         cr4;
    uint64_t         efer;
    uint64_t         cr8;
    uint64_t         apic_base;
    uint64_t         xcr0;
    uint64_t         dr[4];
    uint64_t         dr6;

    uint64_t         msr_star;
    uint64_t         msr_lstar;
    uint64_t         msr_cstar;
    uint64_t         msr_sfmask;
    uint64_t         msr_kernel_gs_base;
    uint64_t         msr_tsc_aux;
    uint64_t         msr_misc_enable;
    uint64_t         msr_tsc_adjust;
    uint64_t         msr_feature_control;
    uint64_t         msr_mtrr_def;
    uint64_t         msr_mtrr_fixed[11];
    uint64_t         msr_mtrr_var[16];
    uint64_t         msr_spec_ctrl;
    uint64_t         msr_kvm_system_time;
    uint64_t         msr_kvm_wall_clock;
    uint64_t         mcg_cap;
    uint64_t         mcg_status;
    uint64_t         mcg_ctl;
    uint64_t         mce_banks[KVM_MCE_BANKS * 4];

    bool             guest_loaded;
    uint64_t         host_star, host_lstar, host_cstar, host_sfmask, host_kernel_gs_base, host_tsc_aux;
    uint8_t         *guest_fpu;
    uint8_t         *host_fpu;

    int32_t          pending_irq;
    bool             nmi_pending;
    bool             nmi_injected;
    bool             nmi_masked;
    bool             exc_pending;
    uint8_t          exc_vector;
    bool             exc_has_error;
    uint32_t         exc_error;
    uint32_t         mp_state;
    uint32_t         sipi_vector;

    uint32_t         ncpuid;
    struct kvm_cpuid_entry2 cpuid[KVM_MAX_CPUID];

    kvm_pending_t    pend;
    uint64_t         pio_data_off;
};

struct kvm_vm {
    kmutex_t         lock;
    volatile int     refcount;
    struct mm       *mm;

    spinlock_t       mmu_lock;
    void            *ept_root;
    uint64_t         eptp;
    volatile uint64_t tlb_gen;

    kvm_memslot_t    slots[KVM_MAX_MEMSLOTS];
    volatile uint64_t slots_gen;

    kvm_vcpu_t      *vcpus[KVM_MAX_VCPUS];
    uint32_t         nvcpus;

    spinlock_t       ioevent_lock;
    kvm_ioeventfd_entry_t *ioevents;
    uint32_t         nioevents;

    uint64_t         tss_addr;
    uint64_t         identity_map_addr;
    int64_t          clock_offset_ns;
    uint64_t         mcg_cap_supported;
};

bool     vmx_supported(void);
int      vmx_vcpu_create(kvm_vcpu_t *vcpu);
void     vmx_vcpu_destroy(kvm_vcpu_t *vcpu);
int      vmx_vcpu_run(kvm_vcpu_t *vcpu);
void     vmx_vcpu_reset(kvm_vcpu_t *vcpu);
void     vmx_sched_out(kvm_vcpu_t *vcpu);
void     vmx_sched_in(kvm_vcpu_t *vcpu);
void     vmx_vmcs_enter(kvm_vcpu_t *vcpu);
void     vmx_vmcs_leave(kvm_vcpu_t *vcpu);
void     vmx_get_regs(kvm_vcpu_t *vcpu, struct kvm_regs *r);
void     vmx_set_regs(kvm_vcpu_t *vcpu, const struct kvm_regs *r);
void     vmx_get_sregs(kvm_vcpu_t *vcpu, struct kvm_sregs *s);
int      vmx_set_sregs(kvm_vcpu_t *vcpu, const struct kvm_sregs *s);
void     vmx_get_events(kvm_vcpu_t *vcpu, struct kvm_vcpu_events *e);
int      vmx_set_events(kvm_vcpu_t *vcpu, const struct kvm_vcpu_events *e);
int      vmx_get_msr(kvm_vcpu_t *vcpu, uint32_t index, uint64_t *data, bool host);
int      vmx_set_msr(kvm_vcpu_t *vcpu, uint32_t index, uint64_t data, bool host);
uint64_t vmx_reg_read(kvm_vcpu_t *vcpu, int reg);
void     vmx_reg_write(kvm_vcpu_t *vcpu, int reg, uint64_t val);
uint64_t vmx_supported_cr4(void);
uint32_t vmx_msr_index_list(uint32_t *out, uint32_t max);
uint64_t vmx_tsc_khz(void);
void     vmx_kick(kvm_vcpu_t *vcpu);

int      ept_init(kvm_vm_t *vm);
void     ept_destroy(kvm_vm_t *vm);
int      ept_fault(kvm_vcpu_t *vcpu, uint64_t gpa, uint64_t qual);
void     ept_zap_range(kvm_vm_t *vm, uint64_t gfn, uint64_t npages);
void     ept_write_protect_range(kvm_vm_t *vm, uint64_t gfn, uint64_t npages);
void     ept_flush_all(kvm_vm_t *vm);

kvm_memslot_t *kvm_gfn_slot(kvm_vm_t *vm, uint64_t gfn);
int      kvm_gpa_to_hpa(kvm_vm_t *vm, uint64_t gpa, bool write, uint64_t *hpa);
int      kvm_read_guest(kvm_vm_t *vm, uint64_t gpa, void *buf, uint64_t len);
int      kvm_write_guest(kvm_vm_t *vm, uint64_t gpa, const void *buf, uint64_t len);
void     kvm_mark_dirty(kvm_vm_t *vm, uint64_t gfn);
bool     kvm_ioeventfd_signal(kvm_vm_t *vm, bool pio, uint64_t addr, uint32_t len, uint64_t data);

int      emu_translate(kvm_vcpu_t *vcpu, uint64_t gva, bool write, bool fetch, uint64_t *gpa, uint32_t *pf_error);
int      emu_read_guest_virt(kvm_vcpu_t *vcpu, uint64_t gva, void *buf, uint32_t len);
int      emu_write_guest_virt(kvm_vcpu_t *vcpu, uint64_t gva, const void *buf, uint32_t len);
int      emu_mmio(kvm_vcpu_t *vcpu, uint64_t gpa);
int      emu_mmio_complete(kvm_vcpu_t *vcpu);
uint64_t emu_seg_base(kvm_vcpu_t *vcpu, int seg);
void     emu_inject_pf(kvm_vcpu_t *vcpu, uint64_t gva, uint32_t error);
void     emu_inject_gp(kvm_vcpu_t *vcpu);
void     emu_inject_ud(kvm_vcpu_t *vcpu);

void     kvm_dev_init(void);

#endif
