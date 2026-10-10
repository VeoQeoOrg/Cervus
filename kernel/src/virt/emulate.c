#include "../../include/virt/vmx.h"
#include "../../include/virt/kvm.h"
#include "../../include/io/serial.h"
#include "../../include/syscall/errno.h"
#include <string.h>

#define CR0_PE   (1ull << 0)
#define CR0_WP   (1ull << 16)
#define CR0_PG   (1ull << 31)
#define CR4_PSE  (1ull << 4)
#define CR4_PAE  (1ull << 5)
#define CR4_SMEP (1ull << 20)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)
#define RFLAGS_CF (1ull << 0)
#define RFLAGS_PF (1ull << 2)
#define RFLAGS_AF (1ull << 4)
#define RFLAGS_ZF (1ull << 6)
#define RFLAGS_SF (1ull << 7)
#define RFLAGS_DF (1ull << 10)
#define RFLAGS_OF (1ull << 11)
#define RFLAGS_ARITH (RFLAGS_CF | RFLAGS_PF | RFLAGS_AF | RFLAGS_ZF | RFLAGS_SF | RFLAGS_OF)

#define PG_P  (1ull << 0)
#define PG_RW (1ull << 1)
#define PG_US (1ull << 2)
#define PG_PS (1ull << 7)
#define PG_NX (1ull << 63)
#define PG_ADDR 0x000FFFFFFFFFF000ull

#define PFERR_P (1u << 0)
#define PFERR_W (1u << 1)
#define PFERR_U (1u << 2)
#define PFERR_I (1u << 4)

enum { SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS };

enum {
    EOP_NONE, EOP_LOAD, EOP_LOADX, EOP_ALU_RM, EOP_ALU_REG, EOP_FLAGS, EOP_XCHG,
    EOP_LODS, EOP_MOVS
};

enum { ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP, ALU_TEST };

static int guest_cpl(kvm_vcpu_t *vcpu) {
    if (!(vcpu->cr0 & CR0_PE)) return 0;
    return (int)((vmread(VMCS_GUEST_SS_AR) >> 5) & 3);
}

static int read_pte(kvm_vcpu_t *vcpu, uint64_t gpa, uint64_t *out, int width) {
    *out = 0;
    return kvm_read_guest(vcpu->vm, gpa, out, (uint64_t)width);
}

int emu_translate(kvm_vcpu_t *vcpu, uint64_t gva, bool write, bool fetch, uint64_t *gpa, uint32_t *pferr) {
    bool lma = (vcpu->efer & EFER_LMA) != 0;
    if (!(vcpu->cr0 & CR0_PG)) {
        *gpa = lma ? gva : (gva & 0xFFFFFFFFull);
        return 0;
    }
    bool user = guest_cpl(vcpu) == 3;
    bool nxe = (vcpu->efer & EFER_NXE) != 0;
    uint32_t err = (write ? PFERR_W : 0) | (user ? PFERR_U : 0) | ((fetch && nxe) ? PFERR_I : 0);
    bool writable = true, usr = true, nx = false;
    uint64_t cr3 = vmread(VMCS_GUEST_CR3);
    uint64_t base = 0, offmask = 0xFFF;
    uint64_t e;

    if (lma) {
        uint64_t table = cr3 & PG_ADDR;
        for (int level = 4; level >= 1; level--) {
            uint64_t idx = (gva >> (12 + 9 * (level - 1))) & 511;
            if (read_pte(vcpu, table + idx * 8, &e, 8) < 0) goto fault_np;
            if (!(e & PG_P)) goto fault_np;
            writable &= (e & PG_RW) != 0;
            usr &= (e & PG_US) != 0;
            if (nxe && (e & PG_NX)) nx = true;
            if ((level == 3 || level == 2) && (e & PG_PS)) {
                offmask = (1ull << (12 + 9 * (level - 1))) - 1;
                base = e & PG_ADDR & ~offmask;
                break;
            }
            table = e & PG_ADDR;
            if (level == 1) base = table;
        }
    } else if (vcpu->cr4 & CR4_PAE) {
        static const uint32_t pdpte_field[4] = { VMCS_GUEST_PDPTE0, VMCS_GUEST_PDPTE1, VMCS_GUEST_PDPTE2, VMCS_GUEST_PDPTE3 };
        uint64_t pdpte = vmread(pdpte_field[(gva >> 30) & 3]);
        if (!(pdpte & PG_P)) goto fault_np;
        uint64_t table = pdpte & PG_ADDR;
        if (read_pte(vcpu, table + ((gva >> 21) & 511) * 8, &e, 8) < 0 || !(e & PG_P)) goto fault_np;
        writable &= (e & PG_RW) != 0;
        usr &= (e & PG_US) != 0;
        if (nxe && (e & PG_NX)) nx = true;
        if (e & PG_PS) {
            offmask = (1ull << 21) - 1;
            base = e & PG_ADDR & ~offmask;
        } else {
            table = e & PG_ADDR;
            if (read_pte(vcpu, table + ((gva >> 12) & 511) * 8, &e, 8) < 0 || !(e & PG_P)) goto fault_np;
            writable &= (e & PG_RW) != 0;
            usr &= (e & PG_US) != 0;
            if (nxe && (e & PG_NX)) nx = true;
            base = e & PG_ADDR;
        }
    } else {
        uint64_t table = cr3 & 0xFFFFF000ull;
        uint32_t gva32 = (uint32_t)gva;
        if (read_pte(vcpu, table + ((gva32 >> 22) & 1023) * 4, &e, 4) < 0 || !(e & PG_P)) goto fault_np;
        writable &= (e & PG_RW) != 0;
        usr &= (e & PG_US) != 0;
        if ((e & PG_PS) && (vcpu->cr4 & CR4_PSE)) {
            offmask = (1ull << 22) - 1;
            base = (e & 0xFFC00000ull) | (((e >> 13) & 0xFF) << 32);
        } else {
            table = e & 0xFFFFF000ull;
            if (read_pte(vcpu, table + ((gva32 >> 12) & 1023) * 4, &e, 4) < 0 || !(e & PG_P)) goto fault_np;
            writable &= (e & PG_RW) != 0;
            usr &= (e & PG_US) != 0;
            base = e & 0xFFFFF000ull;
        }
        gva = gva32;
    }

    if (write && !writable && (user || (vcpu->cr0 & CR0_WP))) goto fault_p;
    if (user && !usr) goto fault_p;
    if (fetch && nx) goto fault_p;
    if (fetch && !user && usr && (vcpu->cr4 & CR4_SMEP)) goto fault_p;
    *gpa = base | (gva & offmask);
    return 0;
fault_p:
    *pferr = err | PFERR_P;
    return -1;
fault_np:
    *pferr = err;
    return -1;
}

static int access_virt(kvm_vcpu_t *vcpu, uint64_t gva, void *buf, uint32_t len, bool write) {
    uint8_t *p = buf;
    while (len) {
        uint32_t chunk = 4096 - (uint32_t)(gva & 0xFFF);
        if (chunk > len) chunk = len;
        uint64_t gpa;
        uint32_t err;
        if (emu_translate(vcpu, gva, write, false, &gpa, &err) < 0) {
            emu_inject_pf(vcpu, gva, err);
            return -1;
        }
        int r = write ? kvm_write_guest(vcpu->vm, gpa, p, chunk) : kvm_read_guest(vcpu->vm, gpa, p, chunk);
        if (r < 0) return -2;
        p += chunk; gva += chunk; len -= chunk;
    }
    return 0;
}

int emu_read_guest_virt(kvm_vcpu_t *vcpu, uint64_t gva, void *buf, uint32_t len) {
    return access_virt(vcpu, gva, buf, len, false);
}

int emu_write_guest_virt(kvm_vcpu_t *vcpu, uint64_t gva, const void *buf, uint32_t len) {
    return access_virt(vcpu, gva, (void *)buf, len, true);
}

typedef struct {
    uint8_t  b[16];
    int      n;
    int      pos;
    bool     fault;
    uint64_t fault_addr;
    uint32_t fault_err;
    bool     mode64;
    int      opsize;
    int      addrsize;
    int      seg;
    int      rep;
    bool     has_rex, rex_w, rex_r, rex_x, rex_b;
    uint8_t  op;
    bool     twobyte;
    uint8_t  modrm, mod, reg, rm;
} insn_t;

static int fetch_insn(kvm_vcpu_t *vcpu, insn_t *in) {
    uint64_t rip = vmread(VMCS_GUEST_RIP);
    uint64_t lin = vmread(VMCS_GUEST_CS_BASE) + rip;
    if (!in->mode64) lin &= 0xFFFFFFFFull;
    in->n = 0;
    while (in->n < 15) {
        uint64_t gpa;
        uint32_t err;
        if (emu_translate(vcpu, lin + in->n, false, true, &gpa, &err) < 0) {
            in->fault = true;
            in->fault_addr = lin + in->n;
            in->fault_err = err;
            break;
        }
        uint32_t chunk = 4096 - (uint32_t)((lin + in->n) & 0xFFF);
        if (chunk > (uint32_t)(15 - in->n)) chunk = (uint32_t)(15 - in->n);
        if (kvm_read_guest(vcpu->vm, gpa, in->b + in->n, chunk) < 0) break;
        in->n += (int)chunk;
    }
    return in->n;
}

static bool need(insn_t *in, int k) {
    return in->pos + k <= in->n;
}

static int next_byte(insn_t *in) {
    if (!need(in, 1)) return -1;
    return in->b[in->pos++];
}

static bool read_imm(insn_t *in, int size, int64_t *out) {
    if (!need(in, size)) return false;
    uint64_t v = 0;
    memcpy(&v, in->b + in->pos, (size_t)size);
    in->pos += size;
    if (size == 1) *out = (int8_t)v;
    else if (size == 2) *out = (int16_t)v;
    else if (size == 4) *out = (int32_t)v;
    else *out = (int64_t)v;
    return true;
}

static bool parse_modrm(insn_t *in) {
    int m = next_byte(in);
    if (m < 0) return false;
    in->modrm = (uint8_t)m;
    in->mod = (uint8_t)(m >> 6);
    in->reg = (uint8_t)(((m >> 3) & 7) | (in->rex_r ? 8 : 0));
    in->rm = (uint8_t)((m & 7) | (in->rex_b ? 8 : 0));
    if (in->mod == 3) return true;
    int disp = 0;
    if (in->addrsize == 2) {
        if (in->mod == 0 && (m & 7) == 6) disp = 2;
        else if (in->mod == 1) disp = 1;
        else if (in->mod == 2) disp = 2;
    } else {
        if ((m & 7) == 4) {
            int sib = next_byte(in);
            if (sib < 0) return false;
            if (in->mod == 0 && (sib & 7) == 5) disp = 4;
        }
        if (in->mod == 0 && (m & 7) == 5) disp = 4;
        else if (in->mod == 1) disp = 1;
        else if (in->mod == 2) disp = 4;
    }
    if (!need(in, disp)) return false;
    in->pos += disp;
    return true;
}

static uint64_t size_mask(int size) {
    return size >= 8 ? ~0ull : ((1ull << (size * 8)) - 1);
}

static uint64_t sext(uint64_t v, int size) {
    int shift = 64 - size * 8;
    return size >= 8 ? v : (uint64_t)((int64_t)(v << shift) >> shift);
}

static uint64_t greg_get(kvm_vcpu_t *vcpu, int reg, bool high, int size) {
    uint64_t v = vmx_reg_read(vcpu, reg);
    if (high) return (v >> 8) & 0xFF;
    return v & size_mask(size);
}

static void greg_set(kvm_vcpu_t *vcpu, int reg, bool high, int size, uint64_t val) {
    uint64_t v = vmx_reg_read(vcpu, reg);
    if (high) v = (v & ~0xFF00ull) | ((val & 0xFF) << 8);
    else if (size == 1) v = (v & ~0xFFull) | (val & 0xFF);
    else if (size == 2) v = (v & ~0xFFFFull) | (val & 0xFFFF);
    else if (size == 4) v = val & 0xFFFFFFFFull;
    else v = val;
    vmx_reg_write(vcpu, reg, v);
}

static void byte_reg(insn_t *in, int reg, int size, uint8_t *out_reg, bool *out_high) {
    if (size == 1 && !in->has_rex && reg >= 4 && reg < 8) {
        *out_reg = (uint8_t)(reg - 4);
        *out_high = true;
    } else {
        *out_reg = (uint8_t)reg;
        *out_high = false;
    }
}

static bool parity8(uint64_t v) {
    return !(__builtin_popcount((unsigned)(v & 0xFF)) & 1);
}

static uint64_t alu_exec(kvm_vcpu_t *vcpu, int op, uint64_t a, uint64_t b, int size) {
    (void)vcpu;
    uint64_t mask = size_mask(size);
    uint64_t sign = 1ull << (size * 8 - 1);
    uint64_t fl = vmread(VMCS_GUEST_RFLAGS);
    uint64_t cin = fl & RFLAGS_CF;
    a &= mask; b &= mask;
    uint64_t r = 0;
    bool cf = false, of = false, af = false;
    switch (op) {
    case ALU_ADD:
    case ALU_ADC: {
        uint64_t c = op == ALU_ADC ? cin : 0;
        r = (a + b + c) & mask;
        cf = c ? (r <= a) : (r < a);
        of = ((a ^ r) & (b ^ r) & sign) != 0;
        af = ((a ^ b ^ r) & 0x10) != 0;
        break;
    }
    case ALU_SUB:
    case ALU_CMP:
    case ALU_SBB: {
        uint64_t c = op == ALU_SBB ? cin : 0;
        r = (a - b - c) & mask;
        cf = c ? (a <= b) : (a < b);
        of = ((a ^ b) & (a ^ r) & sign) != 0;
        af = ((a ^ b ^ r) & 0x10) != 0;
        break;
    }
    case ALU_AND:
    case ALU_TEST: r = a & b; break;
    case ALU_OR:   r = a | b; break;
    case ALU_XOR:  r = a ^ b; break;
    }
    fl &= ~RFLAGS_ARITH;
    if (cf) fl |= RFLAGS_CF;
    if (of) fl |= RFLAGS_OF;
    if (af) fl |= RFLAGS_AF;
    if (!r) fl |= RFLAGS_ZF;
    if (r & sign) fl |= RFLAGS_SF;
    if (parity8(r)) fl |= RFLAGS_PF;
    vmwrite(VMCS_GUEST_RFLAGS, fl);
    return r;
}

static void finish_insn(kvm_vcpu_t *vcpu, uint64_t next_rip) {
    bool m64 = (vcpu->efer & EFER_LMA) && ((vmread(VMCS_GUEST_CS_AR) >> 13) & 1);
    vmwrite(VMCS_GUEST_RIP, m64 ? next_rip : (next_rip & 0xFFFFFFFFull));
    uint32_t intr = (uint32_t)vmread(VMCS_GUEST_INTERRUPTIBILITY);
    if (intr & 3) vmwrite(VMCS_GUEST_INTERRUPTIBILITY, intr & ~3u);
}

static int mmio_exit(kvm_vcpu_t *vcpu, uint64_t gpa, int size, bool write, uint64_t data) {
    struct kvm_run *run = vcpu->run;
    run->exit_reason = KVM_EXIT_MMIO;
    run->mmio.phys_addr = gpa;
    run->mmio.len = (uint32_t)size;
    run->mmio.is_write = write ? 1 : 0;
    memset(run->mmio.data, 0, sizeof run->mmio.data);
    if (write) memcpy(run->mmio.data, &data, (size_t)size);
    return 0;
}

static int mmio_write(kvm_vcpu_t *vcpu, uint64_t gpa, int size, uint64_t data) {
    if (kvm_ioeventfd_signal(vcpu->vm, false, gpa, (uint32_t)size, data)) return 1;
    return mmio_exit(vcpu, gpa, size, true, data);
}

static int emu_fail(kvm_vcpu_t *vcpu, insn_t *in, uint64_t gpa) {
    struct kvm_run *run = vcpu->run;
    serial_printf("[kvm] cannot emulate mmio at %llx rip=%llx bytes:", gpa, vmread(VMCS_GUEST_RIP));
    for (int i = 0; i < in->n && i < 15; i++) serial_printf(" %02x", in->b[i]);
    serial_printf("\n");
    run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
    run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
    run->internal.ndata = 3;
    run->internal.data[0] = gpa;
    memcpy(&run->internal.data[1], in->b, 16);
    return 0;
}

static bool is_mmio_gpa(kvm_vcpu_t *vcpu, uint64_t gpa, bool write) {
    kvm_memslot_t *s = kvm_gfn_slot(vcpu->vm, gpa >> 12);
    if (!s) return true;
    return write && (s->flags & KVM_MEM_READONLY);
}

static uint64_t addr_mask(int addrsize) {
    return addrsize == 2 ? 0xFFFFull : addrsize == 4 ? 0xFFFFFFFFull : ~0ull;
}

static uint64_t string_linear(kvm_vcpu_t *vcpu, int seg, int reg, int addrsize) {
    uint64_t off = vmx_reg_read(vcpu, reg) & addr_mask(addrsize);
    uint64_t lin = emu_seg_base(vcpu, seg) + off;
    if (addrsize != 8 && !((vcpu->efer & EFER_LMA) && ((vmread(VMCS_GUEST_CS_AR) >> 13) & 1)))
        lin &= 0xFFFFFFFFull;
    return lin;
}

static void string_advance(kvm_vcpu_t *vcpu, int reg, int addrsize, int size) {
    uint64_t m = addr_mask(addrsize);
    uint64_t v = vmx_reg_read(vcpu, reg);
    uint64_t off = v & m;
    off = (vmread(VMCS_GUEST_RFLAGS) & RFLAGS_DF) ? off - (uint64_t)size : off + (uint64_t)size;
    vmx_reg_write(vcpu, reg, (v & ~m) | (off & m));
}

static bool string_rep_done(kvm_vcpu_t *vcpu, bool rep, int addrsize) {
    if (!rep) return true;
    uint64_t m = addr_mask(addrsize);
    uint64_t v = vmx_reg_read(vcpu, VCPU_REG_RCX);
    uint64_t c = ((v & m) - 1) & m;
    vmx_reg_write(vcpu, VCPU_REG_RCX, (v & ~m) | c);
    return c == 0;
}

int emu_mmio(kvm_vcpu_t *vcpu, uint64_t gpa) {
    insn_t in;
    kvm_pending_t *p = &vcpu->pend;
    memset(&in, 0, sizeof in);
    uint64_t cs_ar = vmread(VMCS_GUEST_CS_AR);
    in.mode64 = (vcpu->efer & EFER_LMA) && ((cs_ar >> 13) & 1);
    bool def32 = in.mode64 || ((cs_ar >> 14) & 1);
    in.opsize = def32 ? 4 : 2;
    in.addrsize = in.mode64 ? 8 : def32 ? 4 : 2;
    in.seg = -1;
    fetch_insn(vcpu, &in);
    if (in.n == 0 && in.fault) {
        emu_inject_pf(vcpu, in.fault_addr, in.fault_err);
        return 1;
    }

    for (;;) {
        int c = next_byte(&in);
        if (c < 0) goto truncated;
        if (c == 0x66) { in.opsize = def32 ? 2 : 4; continue; }
        if (c == 0x67) { in.addrsize = in.mode64 ? 4 : (def32 ? 2 : 4); continue; }
        if (c == 0xF2 || c == 0xF3) { in.rep = c; continue; }
        if (c == 0xF0) continue;
        if (c == 0x26) { in.seg = SEG_ES; continue; }
        if (c == 0x2E) { in.seg = SEG_CS; continue; }
        if (c == 0x36) { in.seg = SEG_SS; continue; }
        if (c == 0x3E) { in.seg = SEG_DS; continue; }
        if (c == 0x64) { in.seg = SEG_FS; continue; }
        if (c == 0x65) { in.seg = SEG_GS; continue; }
        if (in.mode64 && c >= 0x40 && c <= 0x4F) {
            in.has_rex = true;
            in.rex_w = c & 8; in.rex_r = c & 4; in.rex_x = c & 2; in.rex_b = c & 1;
            c = next_byte(&in);
            if (c < 0) goto truncated;
        }
        in.op = (uint8_t)c;
        break;
    }
    if (in.rex_w) in.opsize = 8;
    if (in.op == 0x0F) {
        int c = next_byte(&in);
        if (c < 0) goto truncated;
        in.twobyte = true;
        in.op = (uint8_t)c;
    }

    memset(p, 0, sizeof *p);
    p->kind = PEND_MMIO;
    p->gpa = gpa;
    uint8_t op = in.op;
    int size = in.opsize;
    int64_t imm = 0;

    if (!in.twobyte) {
        if (op == 0x88 || op == 0x89 || op == 0x8A || op == 0x8B) {
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            uint8_t r; bool hi;
            byte_reg(&in, in.reg, size, &r, &hi);
            uint64_t next = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            if (op <= 0x89) {
                uint64_t v = greg_get(vcpu, r, hi, size);
                finish_insn(vcpu, next);
                return mmio_write(vcpu, gpa, size, v);
            }
            p->op = EOP_LOAD; p->size = (uint8_t)size; p->dst_reg = r; p->high_byte = hi;
            p->dst_size = (uint8_t)size; p->next_rip = next;
            goto do_read;
        }
        if (op == 0xC6 || op == 0xC7) {
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3 || (in.reg & 7) != 0) goto unsupported;
            if (!read_imm(&in, size == 8 ? 4 : size, &imm)) goto truncated;
            finish_insn(vcpu, vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos);
            return mmio_write(vcpu, gpa, size, (uint64_t)imm & size_mask(size));
        }
        if (op >= 0xA0 && op <= 0xA3) {
            size = (op & 1) ? in.opsize : 1;
            if (!need(&in, in.addrsize)) goto truncated;
            in.pos += in.addrsize;
            uint64_t next = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            if (op >= 0xA2) {
                uint64_t v = greg_get(vcpu, VCPU_REG_RAX, false, size);
                finish_insn(vcpu, next);
                return mmio_write(vcpu, gpa, size, v);
            }
            p->op = EOP_LOAD; p->size = (uint8_t)size; p->dst_reg = VCPU_REG_RAX;
            p->dst_size = (uint8_t)size; p->next_rip = next;
            goto do_read;
        }
        if (op == 0x63 && in.mode64) {
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            p->op = EOP_LOADX; p->size = 4; p->sign_extend = true;
            p->dst_reg = in.reg; p->dst_size = (uint8_t)in.opsize;
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op < 0x40 && (op & 7) < 4) {
            int alu = op >> 3;
            bool to_rm = !(op & 2);
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            uint8_t r; bool hi;
            byte_reg(&in, in.reg, size, &r, &hi);
            p->size = (uint8_t)size; p->dst_reg = r; p->high_byte = hi; p->dst_size = (uint8_t)size;
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            if (to_rm) {
                p->op = alu == ALU_CMP ? EOP_FLAGS : EOP_ALU_RM;
                p->rmw_operand = greg_get(vcpu, r, hi, size);
            } else {
                p->op = EOP_ALU_REG;
            }
            p->dst_size = (uint8_t)size;
            p->sign_extend = false;
            p->is_write = false;
            p->rep = false;
            p->string = false;
            p->phase = (uint8_t)alu;
            goto do_read;
        }
        if (op == 0x80 || op == 0x81 || op == 0x83) {
            size = op == 0x80 ? 1 : in.opsize;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            int isz = (op == 0x81) ? (size == 8 ? 4 : size) : 1;
            if (!read_imm(&in, isz, &imm)) goto truncated;
            int alu = in.reg & 7;
            p->op = alu == ALU_CMP ? EOP_FLAGS : EOP_ALU_RM;
            p->phase = (uint8_t)alu;
            p->size = (uint8_t)size;
            p->rmw_operand = (uint64_t)imm & size_mask(size);
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op == 0x84 || op == 0x85) {
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            uint8_t r; bool hi;
            byte_reg(&in, in.reg, size, &r, &hi);
            p->op = EOP_FLAGS; p->phase = ALU_TEST; p->size = (uint8_t)size;
            p->rmw_operand = greg_get(vcpu, r, hi, size);
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op == 0xF6 || op == 0xF7) {
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3 || (in.reg & 7) > 1) goto unsupported;
            if (!read_imm(&in, size == 8 ? 4 : size, &imm)) goto truncated;
            p->op = EOP_FLAGS; p->phase = ALU_TEST; p->size = (uint8_t)size;
            p->rmw_operand = (uint64_t)imm & size_mask(size);
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op == 0x86 || op == 0x87) {
            size = (op & 1) ? in.opsize : 1;
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            uint8_t r; bool hi;
            byte_reg(&in, in.reg, size, &r, &hi);
            p->op = EOP_XCHG; p->size = (uint8_t)size; p->dst_reg = r; p->high_byte = hi;
            p->dst_size = (uint8_t)size;
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAD || op == 0xA4 || op == 0xA5) {
            size = (op & 1) ? in.opsize : 1;
            bool rep = in.rep != 0;
            uint64_t next = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            if (rep && !(vmx_reg_read(vcpu, VCPU_REG_RCX) & addr_mask(in.addrsize))) {
                finish_insn(vcpu, next);
                return 1;
            }
            p->rep = rep; p->addr_size = (uint8_t)in.addrsize; p->size = (uint8_t)size;
            p->next_rip = next; p->string = true;
            int src_seg = in.seg >= 0 ? in.seg : SEG_DS;
            if (op == 0xAA || op == 0xAB) {
                uint64_t v = greg_get(vcpu, VCPU_REG_RAX, false, size);
                string_advance(vcpu, VCPU_REG_RDI, in.addrsize, size);
                if (string_rep_done(vcpu, rep, in.addrsize)) finish_insn(vcpu, next);
                memset(p, 0, sizeof *p);
                return mmio_write(vcpu, gpa, size, v);
            }
            if (op == 0xAC || op == 0xAD) {
                p->op = EOP_LODS;
                p->pio_seg = src_seg;
                goto do_read;
            }
            uint64_t src_lin = string_linear(vcpu, src_seg, VCPU_REG_RSI, in.addrsize);
            uint64_t dst_lin = string_linear(vcpu, SEG_ES, VCPU_REG_RDI, in.addrsize);
            uint64_t src_gpa, dst_gpa;
            uint32_t err;
            if (emu_translate(vcpu, src_lin, false, false, &src_gpa, &err) < 0) { emu_inject_pf(vcpu, src_lin, err); memset(p, 0, sizeof *p); return 1; }
            if (emu_translate(vcpu, dst_lin, true, false, &dst_gpa, &err) < 0) { emu_inject_pf(vcpu, dst_lin, err); memset(p, 0, sizeof *p); return 1; }
            bool src_mmio = is_mmio_gpa(vcpu, src_gpa, false);
            if (!src_mmio) {
                uint64_t v = 0;
                if (kvm_read_guest(vcpu->vm, src_gpa, &v, (uint64_t)size) < 0) goto unsupported;
                string_advance(vcpu, VCPU_REG_RSI, in.addrsize, size);
                string_advance(vcpu, VCPU_REG_RDI, in.addrsize, size);
                if (string_rep_done(vcpu, rep, in.addrsize)) finish_insn(vcpu, next);
                memset(p, 0, sizeof *p);
                if (is_mmio_gpa(vcpu, dst_gpa, true)) return mmio_write(vcpu, dst_gpa, size, v);
                kvm_write_guest(vcpu->vm, dst_gpa, &v, (uint64_t)size);
                return 1;
            }
            p->op = EOP_MOVS;
            p->gpa = src_gpa;
            p->rmw_operand = dst_gpa;
            gpa = src_gpa;
            goto do_read;
        }
    } else {
        if (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF) {
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            p->op = EOP_LOADX;
            p->size = (op & 1) ? 2 : 1;
            p->sign_extend = op >= 0xBE;
            p->dst_reg = in.reg;
            p->dst_size = (uint8_t)in.opsize;
            p->next_rip = vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos;
            goto do_read;
        }
        if (op == 0xC3) {
            if (!parse_modrm(&in)) goto truncated;
            if (in.mod == 3) goto unsupported;
            uint64_t v = greg_get(vcpu, in.reg, false, in.opsize);
            finish_insn(vcpu, vmread(VMCS_GUEST_RIP) + (uint64_t)in.pos);
            return mmio_write(vcpu, gpa, in.opsize, v);
        }
    }

unsupported:
    memset(p, 0, sizeof *p);
    return emu_fail(vcpu, &in, gpa);

truncated:
    memset(p, 0, sizeof *p);
    if (in.fault) {
        emu_inject_pf(vcpu, in.fault_addr, in.fault_err);
        return 1;
    }
    return emu_fail(vcpu, &in, gpa);

do_read:
    p->active = true;
    p->kind = PEND_MMIO;
    return mmio_exit(vcpu, gpa, p->size, false, 0);
}

int emu_mmio_complete(kvm_vcpu_t *vcpu) {
    kvm_pending_t *p = &vcpu->pend;
    struct kvm_run *run = vcpu->run;
    p->active = false;
    uint64_t val = 0;
    memcpy(&val, run->mmio.data, p->size <= 8 ? p->size : 8);
    val &= size_mask(p->size);
    int size = p->size;

    switch (p->op) {
    case EOP_LOAD:
        greg_set(vcpu, p->dst_reg, p->high_byte, size, val);
        finish_insn(vcpu, p->next_rip);
        return 1;
    case EOP_LOADX: {
        uint64_t v = p->sign_extend ? sext(val, size) : val;
        greg_set(vcpu, p->dst_reg, false, p->dst_size, v & size_mask(p->dst_size));
        finish_insn(vcpu, p->next_rip);
        return 1;
    }
    case EOP_FLAGS:
        if (p->phase == ALU_TEST) alu_exec(vcpu, ALU_TEST, val, p->rmw_operand, size);
        else alu_exec(vcpu, ALU_CMP, val, p->rmw_operand, size);
        finish_insn(vcpu, p->next_rip);
        return 1;
    case EOP_ALU_REG: {
        uint64_t a = greg_get(vcpu, p->dst_reg, p->high_byte, size);
        uint64_t r = alu_exec(vcpu, p->phase, a, val, size);
        if (p->phase != ALU_CMP) greg_set(vcpu, p->dst_reg, p->high_byte, size, r);
        finish_insn(vcpu, p->next_rip);
        return 1;
    }
    case EOP_ALU_RM: {
        uint64_t r = alu_exec(vcpu, p->phase, val, p->rmw_operand, size);
        finish_insn(vcpu, p->next_rip);
        return mmio_write(vcpu, p->gpa, size, r);
    }
    case EOP_XCHG: {
        uint64_t regv = greg_get(vcpu, p->dst_reg, p->high_byte, size);
        greg_set(vcpu, p->dst_reg, p->high_byte, size, val);
        finish_insn(vcpu, p->next_rip);
        return mmio_write(vcpu, p->gpa, size, regv);
    }
    case EOP_LODS:
        greg_set(vcpu, VCPU_REG_RAX, false, size, val);
        string_advance(vcpu, VCPU_REG_RSI, p->addr_size, size);
        if (string_rep_done(vcpu, p->rep, p->addr_size)) finish_insn(vcpu, p->next_rip);
        return 1;
    case EOP_MOVS: {
        uint64_t dst = p->rmw_operand;
        string_advance(vcpu, VCPU_REG_RSI, p->addr_size, size);
        string_advance(vcpu, VCPU_REG_RDI, p->addr_size, size);
        if (string_rep_done(vcpu, p->rep, p->addr_size)) finish_insn(vcpu, p->next_rip);
        if (is_mmio_gpa(vcpu, dst, true)) return mmio_write(vcpu, dst, size, val);
        kvm_write_guest(vcpu->vm, dst, &val, (uint64_t)size);
        return 1;
    }
    default:
        return 1;
    }
}
