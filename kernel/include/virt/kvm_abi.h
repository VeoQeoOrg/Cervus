#ifndef VIRT_KVM_ABI_H
#define VIRT_KVM_ABI_H

#include <stdint.h>

#define KVM_API_VERSION 12

#define KVM_IOC_NONE  0u
#define KVM_IOC_WRITE 1u
#define KVM_IOC_READ  2u
#define KVM_IOC(dir, nr, size) \
    ((uint32_t)(((dir) << 30) | ((uint32_t)(size) << 16) | (0xAEu << 8) | (nr)))
#define KVM_IO(nr)            KVM_IOC(KVM_IOC_NONE, nr, 0)
#define KVM_IOR(nr, type)     KVM_IOC(KVM_IOC_READ, nr, sizeof(type))
#define KVM_IOW(nr, type)     KVM_IOC(KVM_IOC_WRITE, nr, sizeof(type))
#define KVM_IOWR(nr, type)    KVM_IOC(KVM_IOC_READ | KVM_IOC_WRITE, nr, sizeof(type))
#define KVM_IOC_NR(c)         ((c) & 0xFFu)
#define KVM_IOC_TYPE(c)       (((c) >> 8) & 0xFFu)

struct kvm_userspace_memory_region {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
};

struct kvm_userspace_memory_region2 {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t guest_memfd_offset;
    uint32_t guest_memfd;
    uint32_t pad1;
    uint64_t pad2[14];
};

#define KVM_MEM_LOG_DIRTY_PAGES (1u << 0)
#define KVM_MEM_READONLY        (1u << 1)

struct kvm_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
};

struct kvm_segment {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint8_t  type;
    uint8_t  present, dpl, db, s, l, g, avl;
    uint8_t  unusable;
    uint8_t  padding;
};

struct kvm_dtable {
    uint64_t base;
    uint16_t limit;
    uint16_t padding[3];
};

#define KVM_NR_INTERRUPTS 256

struct kvm_sregs {
    struct kvm_segment cs, ds, es, fs, gs, ss;
    struct kvm_segment tr, ldt;
    struct kvm_dtable gdt, idt;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
    uint64_t interrupt_bitmap[(KVM_NR_INTERRUPTS + 63) / 64];
};

struct kvm_sregs2 {
    struct kvm_segment cs, ds, es, fs, gs, ss;
    struct kvm_segment tr, ldt;
    struct kvm_dtable gdt, idt;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
    uint64_t flags;
    uint64_t pdptrs[4];
};

struct kvm_fpu {
    uint8_t  fpr[8][16];
    uint16_t fcw;
    uint16_t fsw;
    uint8_t  ftwx;
    uint8_t  pad1;
    uint16_t last_opcode;
    uint64_t last_ip;
    uint64_t last_dp;
    uint8_t  xmm[16][16];
    uint32_t mxcsr;
    uint32_t pad2;
};

struct kvm_msr_entry {
    uint32_t index;
    uint32_t reserved;
    uint64_t data;
};

struct kvm_msrs {
    uint32_t nmsrs;
    uint32_t pad;
    struct kvm_msr_entry entries[];
};

struct kvm_msr_list {
    uint32_t nmsrs;
    uint32_t indices[];
};

struct kvm_cpuid_entry2 {
    uint32_t function;
    uint32_t index;
    uint32_t flags;
    uint32_t eax, ebx, ecx, edx;
    uint32_t padding[3];
};

#define KVM_CPUID_FLAG_SIGNIFCANT_INDEX (1u << 0)

struct kvm_cpuid2 {
    uint32_t nent;
    uint32_t padding;
    struct kvm_cpuid_entry2 entries[];
};

struct kvm_cpuid_entry {
    uint32_t function;
    uint32_t eax, ebx, ecx, edx;
    uint32_t padding;
};

struct kvm_cpuid {
    uint32_t nent;
    uint32_t padding;
    struct kvm_cpuid_entry entries[];
};

struct kvm_xsave {
    uint32_t region[1024];
};

#define KVM_MAX_XCRS 16

struct kvm_xcr {
    uint32_t xcr;
    uint32_t reserved;
    uint64_t value;
};

struct kvm_xcrs {
    uint32_t nr_xcrs;
    uint32_t flags;
    struct kvm_xcr xcrs[KVM_MAX_XCRS];
    uint64_t padding[16];
};

struct kvm_vcpu_events {
    struct {
        uint8_t  injected;
        uint8_t  nr;
        uint8_t  has_error_code;
        uint8_t  pending;
        uint32_t error_code;
    } exception;
    struct {
        uint8_t injected;
        uint8_t nr;
        uint8_t soft;
        uint8_t shadow;
    } interrupt;
    struct {
        uint8_t injected;
        uint8_t pending;
        uint8_t masked;
        uint8_t pad;
    } nmi;
    uint32_t sipi_vector;
    uint32_t flags;
    struct {
        uint8_t smm;
        uint8_t pending;
        uint8_t smm_inside_nmi;
        uint8_t latched_init;
    } smi;
    struct {
        uint8_t pending;
    } triple_fault;
    uint8_t  reserved[26];
    uint8_t  exception_has_payload;
    uint64_t exception_payload;
};

#define KVM_VCPUEVENT_VALID_NMI_PENDING  0x01u
#define KVM_VCPUEVENT_VALID_SIPI_VECTOR  0x02u
#define KVM_VCPUEVENT_VALID_SHADOW       0x04u
#define KVM_VCPUEVENT_VALID_SMM          0x08u
#define KVM_VCPUEVENT_VALID_PAYLOAD      0x10u
#define KVM_VCPUEVENT_VALID_TRIPLE_FAULT 0x20u

#define KVM_X86_SHADOW_INT_MOV_SS 0x01u
#define KVM_X86_SHADOW_INT_STI    0x02u

struct kvm_debugregs {
    uint64_t db[4];
    uint64_t dr6;
    uint64_t dr7;
    uint64_t flags;
    uint64_t reserved[9];
};

struct kvm_interrupt {
    uint32_t irq;
};

struct kvm_mp_state {
    uint32_t mp_state;
};

#define KVM_MP_STATE_RUNNABLE      0
#define KVM_MP_STATE_UNINITIALIZED 1
#define KVM_MP_STATE_INIT_RECEIVED 2
#define KVM_MP_STATE_HALTED        3
#define KVM_MP_STATE_SIPI_RECEIVED 4

struct kvm_dirty_log {
    uint32_t slot;
    uint32_t padding1;
    uint64_t dirty_bitmap;
};

struct kvm_clear_dirty_log {
    uint32_t slot;
    uint32_t num_pages;
    uint64_t first_page;
    uint64_t dirty_bitmap;
};

struct kvm_ioeventfd {
    uint64_t datamatch;
    uint64_t addr;
    uint32_t len;
    int32_t  fd;
    uint32_t flags;
    uint8_t  pad[36];
};

#define KVM_IOEVENTFD_FLAG_DATAMATCH (1u << 0)
#define KVM_IOEVENTFD_FLAG_PIO       (1u << 1)
#define KVM_IOEVENTFD_FLAG_DEASSIGN  (1u << 2)

struct kvm_irqfd {
    uint32_t fd;
    uint32_t gsi;
    uint32_t flags;
    uint32_t resamplefd;
    uint8_t  pad[16];
};

struct kvm_msi {
    uint32_t address_lo;
    uint32_t address_hi;
    uint32_t data;
    uint32_t flags;
    uint32_t devid;
    uint8_t  pad[12];
};

struct kvm_irq_routing {
    uint32_t nr;
    uint32_t flags;
};

struct kvm_enable_cap {
    uint32_t cap;
    uint32_t flags;
    uint64_t args[4];
    uint8_t  pad[64];
};

struct kvm_clock_data {
    uint64_t clock;
    uint32_t flags;
    uint32_t pad0;
    uint64_t realtime;
    uint64_t host_tsc;
    uint32_t pad[4];
};

#define KVM_CLOCK_TSC_STABLE 2u

struct kvm_x86_mce {
    uint64_t status;
    uint64_t addr;
    uint64_t misc;
    uint64_t mcg_status;
    uint8_t  bank;
    uint8_t  pad1[7];
    uint64_t pad2[3];
};

struct kvm_signal_mask {
    uint32_t len;
    uint8_t  sigset[];
};

struct kvm_translation {
    uint64_t linear_address;
    uint64_t physical_address;
    uint8_t  valid;
    uint8_t  writeable;
    uint8_t  usermode;
    uint8_t  pad[5];
};

struct kvm_coalesced_mmio_zone {
    uint64_t addr;
    uint32_t size;
    uint32_t pad;
};

struct kvm_tpr_access_ctl {
    uint32_t enabled;
    uint32_t flags;
    uint32_t reserved[8];
};

struct kvm_vapic_addr {
    uint64_t vapic_addr;
};

struct kvm_guest_debug {
    uint32_t control;
    uint32_t pad;
    uint64_t debugreg[8];
};

struct kvm_run {
    uint8_t  request_interrupt_window;
    uint8_t  immediate_exit;
    uint8_t  padding1[6];
    uint32_t exit_reason;
    uint8_t  ready_for_interrupt_injection;
    uint8_t  if_flag;
    uint16_t flags;
    uint64_t cr8;
    uint64_t apic_base;
    union {
        struct {
            uint64_t hardware_exit_reason;
        } hw;
        struct {
            uint64_t hardware_entry_failure_reason;
            uint32_t cpu;
        } fail_entry;
        struct {
            uint32_t exception;
            uint32_t error_code;
        } ex;
        struct {
            uint8_t  direction;
            uint8_t  size;
            uint16_t port;
            uint32_t count;
            uint64_t data_offset;
        } io;
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
        } mmio;
        struct {
            uint32_t suberror;
            uint32_t ndata;
            uint64_t data[16];
        } internal;
        struct {
            uint32_t type;
            uint32_t ndata;
            uint64_t data[16];
        } system_event;
        struct {
            uint8_t  error;
            uint8_t  pad[7];
            uint32_t reason;
            uint32_t index;
            uint64_t data;
        } msr;
        char padding[256];
    };
    uint64_t kvm_valid_regs;
    uint64_t kvm_dirty_regs;
    union {
        char padding[2048];
    } s;
};

#define KVM_EXIT_UNKNOWN         0
#define KVM_EXIT_EXCEPTION       1
#define KVM_EXIT_IO              2
#define KVM_EXIT_HYPERCALL       3
#define KVM_EXIT_DEBUG           4
#define KVM_EXIT_HLT             5
#define KVM_EXIT_MMIO            6
#define KVM_EXIT_IRQ_WINDOW_OPEN 7
#define KVM_EXIT_SHUTDOWN        8
#define KVM_EXIT_FAIL_ENTRY      9
#define KVM_EXIT_INTR            10
#define KVM_EXIT_SET_TPR         11
#define KVM_EXIT_NMI             16
#define KVM_EXIT_INTERNAL_ERROR  17
#define KVM_EXIT_SYSTEM_EVENT    24

#define KVM_EXIT_IO_IN  0
#define KVM_EXIT_IO_OUT 1

#define KVM_INTERNAL_ERROR_EMULATION              1
#define KVM_INTERNAL_ERROR_SIMUL_EX               2
#define KVM_INTERNAL_ERROR_DELIVERY_EV            3
#define KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON 4

#define KVM_SYSTEM_EVENT_SHUTDOWN 1
#define KVM_SYSTEM_EVENT_RESET    2

#define KVM_CAP_IRQCHIP                     0
#define KVM_CAP_HLT                         1
#define KVM_CAP_USER_MEMORY                 3
#define KVM_CAP_SET_TSS_ADDR                4
#define KVM_CAP_EXT_CPUID                   7
#define KVM_CAP_CLOCKSOURCE                 8
#define KVM_CAP_NR_VCPUS                    9
#define KVM_CAP_NR_MEMSLOTS                 10
#define KVM_CAP_NOP_IO_DELAY                12
#define KVM_CAP_MP_STATE                    14
#define KVM_CAP_COALESCED_MMIO              15
#define KVM_CAP_SYNC_MMU                    16
#define KVM_CAP_DESTROY_MEMORY_REGION_WORKS 21
#define KVM_CAP_USER_NMI                    22
#define KVM_CAP_IRQ_ROUTING                 25
#define KVM_CAP_JOIN_MEMORY_REGIONS_WORKS   30
#define KVM_CAP_MCE                         31
#define KVM_CAP_IOEVENTFD                   36
#define KVM_CAP_SET_IDENTITY_MAP_ADDR       37
#define KVM_CAP_ADJUST_CLOCK                39
#define KVM_CAP_INTERNAL_ERROR_DATA         40
#define KVM_CAP_VCPU_EVENTS                 41
#define KVM_CAP_INTR_SHADOW                 49
#define KVM_CAP_DEBUGREGS                   50
#define KVM_CAP_X86_ROBUST_SINGLESTEP       51
#define KVM_CAP_ENABLE_CAP                  54
#define KVM_CAP_XSAVE                       55
#define KVM_CAP_XCRS                        56
#define KVM_CAP_GET_TSC_KHZ                 61
#define KVM_CAP_MAX_VCPUS                   66
#define KVM_CAP_SIGNAL_MSI                  77
#define KVM_CAP_READONLY_MEM                81
#define KVM_CAP_IOEVENTFD_NO_LENGTH         100
#define KVM_CAP_IOEVENTFD_ANY_LENGTH        122
#define KVM_CAP_MAX_VCPU_ID                 128
#define KVM_CAP_IMMEDIATE_EXIT              136

#define KVM_GET_API_VERSION        KVM_IO(0x00)
#define KVM_CREATE_VM              KVM_IO(0x01)
#define KVM_GET_MSR_INDEX_LIST     KVM_IOWR(0x02, struct kvm_msr_list)
#define KVM_CHECK_EXTENSION        KVM_IO(0x03)
#define KVM_GET_VCPU_MMAP_SIZE     KVM_IO(0x04)
#define KVM_GET_SUPPORTED_CPUID    KVM_IOWR(0x05, struct kvm_cpuid2)
#define KVM_GET_EMULATED_CPUID     KVM_IOWR(0x09, struct kvm_cpuid2)
#define KVM_GET_MSR_FEATURE_INDEX_LIST KVM_IOWR(0x0a, struct kvm_msr_list)

#define KVM_CREATE_VCPU            KVM_IO(0x41)
#define KVM_GET_DIRTY_LOG          KVM_IOW(0x42, struct kvm_dirty_log)
#define KVM_SET_NR_MMU_PAGES       KVM_IO(0x44)
#define KVM_GET_NR_MMU_PAGES       KVM_IO(0x45)
#define KVM_SET_USER_MEMORY_REGION KVM_IOW(0x46, struct kvm_userspace_memory_region)
#define KVM_SET_TSS_ADDR           KVM_IO(0x47)
#define KVM_SET_IDENTITY_MAP_ADDR  KVM_IOW(0x48, uint64_t)
#define KVM_SET_USER_MEMORY_REGION2 KVM_IOW(0x49, struct kvm_userspace_memory_region2)
#define KVM_REGISTER_COALESCED_MMIO   KVM_IOW(0x67, struct kvm_coalesced_mmio_zone)
#define KVM_UNREGISTER_COALESCED_MMIO KVM_IOW(0x68, struct kvm_coalesced_mmio_zone)
#define KVM_SET_GSI_ROUTING        KVM_IOW(0x6a, struct kvm_irq_routing)
#define KVM_IRQFD                  KVM_IOW(0x76, struct kvm_irqfd)
#define KVM_SET_BOOT_CPU_ID        KVM_IO(0x78)
#define KVM_IOEVENTFD              KVM_IOW(0x79, struct kvm_ioeventfd)
#define KVM_SET_CLOCK              KVM_IOW(0x7b, struct kvm_clock_data)
#define KVM_GET_CLOCK              KVM_IOR(0x7c, struct kvm_clock_data)
#define KVM_ENABLE_CAP             KVM_IOW(0xa3, struct kvm_enable_cap)
#define KVM_SIGNAL_MSI             KVM_IOW(0xa5, struct kvm_msi)
#define KVM_CLEAR_DIRTY_LOG        KVM_IOWR(0xc0, struct kvm_clear_dirty_log)

#define KVM_RUN                    KVM_IO(0x80)
#define KVM_GET_REGS               KVM_IOR(0x81, struct kvm_regs)
#define KVM_SET_REGS               KVM_IOW(0x82, struct kvm_regs)
#define KVM_GET_SREGS              KVM_IOR(0x83, struct kvm_sregs)
#define KVM_SET_SREGS              KVM_IOW(0x84, struct kvm_sregs)
#define KVM_TRANSLATE              KVM_IOWR(0x85, struct kvm_translation)
#define KVM_INTERRUPT              KVM_IOW(0x86, struct kvm_interrupt)
#define KVM_GET_MSRS               KVM_IOWR(0x88, struct kvm_msrs)
#define KVM_SET_MSRS               KVM_IOW(0x89, struct kvm_msrs)
#define KVM_SET_CPUID              KVM_IOW(0x8a, struct kvm_cpuid)
#define KVM_SET_SIGNAL_MASK        KVM_IOW(0x8b, struct kvm_signal_mask)
#define KVM_GET_FPU                KVM_IOR(0x8c, struct kvm_fpu)
#define KVM_SET_FPU                KVM_IOW(0x8d, struct kvm_fpu)
#define KVM_SET_CPUID2             KVM_IOW(0x90, struct kvm_cpuid2)
#define KVM_GET_CPUID2             KVM_IOWR(0x91, struct kvm_cpuid2)
#define KVM_TPR_ACCESS_REPORTING   KVM_IOWR(0x92, struct kvm_tpr_access_ctl)
#define KVM_SET_VAPIC_ADDR         KVM_IOW(0x93, struct kvm_vapic_addr)
#define KVM_GET_MP_STATE           KVM_IOR(0x98, struct kvm_mp_state)
#define KVM_SET_MP_STATE           KVM_IOW(0x99, struct kvm_mp_state)
#define KVM_NMI                    KVM_IO(0x9a)
#define KVM_SET_GUEST_DEBUG        KVM_IOW(0x9b, struct kvm_guest_debug)
#define KVM_X86_SETUP_MCE          KVM_IOW(0x9c, uint64_t)
#define KVM_X86_GET_MCE_CAP_SUPPORTED KVM_IOR(0x9d, uint64_t)
#define KVM_X86_SET_MCE            KVM_IOW(0x9e, struct kvm_x86_mce)
#define KVM_GET_VCPU_EVENTS        KVM_IOR(0x9f, struct kvm_vcpu_events)
#define KVM_SET_VCPU_EVENTS        KVM_IOW(0xa0, struct kvm_vcpu_events)
#define KVM_GET_DEBUGREGS          KVM_IOR(0xa1, struct kvm_debugregs)
#define KVM_SET_DEBUGREGS          KVM_IOW(0xa2, struct kvm_debugregs)
#define KVM_SET_TSC_KHZ            KVM_IO(0xa2)
#define KVM_GET_TSC_KHZ            KVM_IO(0xa3)
#define KVM_GET_XSAVE              KVM_IOR(0xa4, struct kvm_xsave)
#define KVM_SET_XSAVE              KVM_IOW(0xa5, struct kvm_xsave)
#define KVM_GET_XCRS               KVM_IOR(0xa6, struct kvm_xcrs)
#define KVM_SET_XCRS               KVM_IOW(0xa7, struct kvm_xcrs)
#define KVM_KVMCLOCK_CTRL          KVM_IO(0xad)
#define KVM_GET_SREGS2             KVM_IOR(0xcc, struct kvm_sregs2)
#define KVM_SET_SREGS2             KVM_IOW(0xcd, struct kvm_sregs2)
#define KVM_GET_XSAVE2             KVM_IOR(0xcf, struct kvm_xsave)

#endif
