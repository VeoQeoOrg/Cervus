#ifndef _SYS_USER_H
#define _SYS_USER_H
#ifdef __cplusplus
extern "C" {
#endif

struct user_fpregs_struct {
    unsigned short     cwd, swd, ftw, fop;
    unsigned long long rip, rdp;
    unsigned int       mxcsr, mxcr_mask;
    unsigned int       st_space[32];
    unsigned int       xmm_space[64];
    unsigned int       padding[24];
};

struct user_regs_struct {
    unsigned long long r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
    unsigned long long rax, rcx, rdx, rsi, rdi, orig_rax, rip, cs, eflags, rsp, ss;
    unsigned long long fs_base, gs_base, ds, es, fs, gs;
};

#define ELF_NGREG 27
typedef unsigned long long elf_greg_t, elf_gregset_t[ELF_NGREG];
typedef struct user_fpregs_struct elf_fpregset_t;

struct user {
    struct user_regs_struct    regs;
    int                        u_fpvalid;
    struct user_fpregs_struct  i387;
    unsigned long              u_tsize, u_dsize, u_ssize;
    unsigned long              start_code, start_stack;
    long                       signal;
    int                        reserved;
    struct user_regs_struct   *u_ar0;
    struct user_fpregs_struct *u_fpstate;
    unsigned long              magic;
    char                       u_comm[32];
    unsigned long              u_debugreg[8];
};

#define NBPG   4096
#define UPAGES 1
#define HOST_TEXT_START_ADDR (u.start_code)
#define HOST_STACK_END_ADDR  (u.start_stack + u.u_ssize * NBPG)

#ifdef __cplusplus
}
#endif
#endif
