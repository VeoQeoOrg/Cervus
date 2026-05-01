#ifndef _SYS_UCONTEXT_H
#define _SYS_UCONTEXT_H

#include <stdint.h>
#include <signal.h>

typedef unsigned long greg_t;

#define NGREG 23

typedef greg_t gregset_t[NGREG];

enum {
    REG_R8      =  0,
    REG_R9      =  1,
    REG_R10     =  2,
    REG_R11     =  3,
    REG_R12     =  4,
    REG_R13     =  5,
    REG_R14     =  6,
    REG_R15     =  7,
    REG_RDI     =  8,
    REG_RSI     =  9,
    REG_RBP     = 10,
    REG_RBX     = 11,
    REG_RDX     = 12,
    REG_RAX     = 13,
    REG_RCX     = 14,
    REG_RSP     = 15,
    REG_RIP     = 16,
    REG_EFL     = 17,
    REG_CSGSFS  = 18,
    REG_ERR     = 19,
    REG_TRAPNO  = 20,
    REG_OLDMASK = 21,
    REG_CR2     = 22,
};

typedef struct {
    uint16_t cwd;
    uint16_t swd;
    uint16_t ftw;
    uint16_t fop;
    uint64_t rip;
    uint64_t rdp;
    uint32_t mxcsr;
    uint32_t mxcr_mask;
    uint32_t st_space[32];
    uint32_t xmm_space[64];
    uint32_t padding[24];
} fpregset_t;

typedef struct {
    gregset_t   gregs;
    fpregset_t *fpregs;
    uint64_t    __reserved[8];
} mcontext_t;

typedef struct ucontext {
    uint64_t         uc_flags;
    struct ucontext *uc_link;
    stack_t          uc_stack;
    mcontext_t       uc_mcontext;
    sigset_t         uc_sigmask;
} ucontext_t;

#endif