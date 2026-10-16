#ifndef KERNEL_SCHED_PTRACE_H
#define KERNEL_SCHED_PTRACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct task;
struct int_frame_t;

#define PTRACE_TRACEME          0
#define PTRACE_PEEKTEXT         1
#define PTRACE_PEEKDATA         2
#define PTRACE_PEEKUSER         3
#define PTRACE_POKETEXT         4
#define PTRACE_POKEDATA         5
#define PTRACE_POKEUSER         6
#define PTRACE_CONT             7
#define PTRACE_KILL             8
#define PTRACE_SINGLESTEP       9
#define PTRACE_GETREGS          12
#define PTRACE_SETREGS          13
#define PTRACE_GETFPREGS        14
#define PTRACE_SETFPREGS        15
#define PTRACE_ATTACH           16
#define PTRACE_DETACH           17
#define PTRACE_SYSCALL          24
#define PTRACE_ARCH_PRCTL       30
#define PTRACE_SETOPTIONS       0x4200
#define PTRACE_GETEVENTMSG      0x4201
#define PTRACE_GETSIGINFO       0x4202
#define PTRACE_SETSIGINFO       0x4203
#define PTRACE_GETREGSET        0x4204
#define PTRACE_SETREGSET        0x4205
#define PTRACE_SEIZE            0x4206
#define PTRACE_INTERRUPT        0x4207
#define PTRACE_LISTEN           0x4208
#define PTRACE_GETSIGMASK       0x420a
#define PTRACE_SETSIGMASK       0x420b

#define PTRACE_O_TRACESYSGOOD   0x01
#define PTRACE_O_TRACEFORK      0x02
#define PTRACE_O_TRACEVFORK     0x04
#define PTRACE_O_TRACECLONE     0x08
#define PTRACE_O_TRACEEXEC      0x10
#define PTRACE_O_TRACEVFORKDONE 0x20
#define PTRACE_O_TRACEEXIT      0x40
#define PTRACE_O_TRACESECCOMP   0x80
#define PTRACE_O_EXITKILL       0x100000
#define PTRACE_O_MASK           0x1000ff

#define PTRACE_EVENT_FORK       1
#define PTRACE_EVENT_VFORK      2
#define PTRACE_EVENT_CLONE      3
#define PTRACE_EVENT_EXEC       4
#define PTRACE_EVENT_VFORK_DONE 5
#define PTRACE_EVENT_EXIT       6
#define PTRACE_EVENT_STOP       128

#define PT_TRACED     (1u << 0)
#define PT_SEIZED     (1u << 1)
#define PT_SYSCALL    (1u << 2)
#define PT_STEP       (1u << 3)
#define PT_STOPPED    (1u << 4)
#define PT_NOTIFY     (1u << 5)
#define PT_TF_SET     (1u << 6)

#define PT_PEND_EXEC  (1u << 0)
#define PT_PEND_EVENT (1u << 1)
#define PT_PEND_TRAP  (1u << 2)

#define PT_CTX_SYSCALL 1
#define PT_CTX_FRAME   2

typedef struct {
    int                 kind;
    struct int_frame_t *f;
    int64_t            *rax;
    uint64_t            orig_rax;
} pt_ctx_t;

int      ptrace_stop(struct task *t, int status, pt_ctx_t *ctx);
int      ptrace_signal_stop(struct task *t, int sig, void *si, pt_ctx_t *ctx);
void     ptrace_syscall_enter(struct task *t, uint64_t *nr, uint64_t args[6], int64_t *ret, bool *skip);
void     ptrace_syscall_exit(struct task *t, uint64_t nr, int64_t *ret);
void     ptrace_note_exec(struct task *t);
void     ptrace_note_child(struct task *parent, struct task *child, int event);
void     ptrace_task_exit(struct task *t, int status);
void     ptrace_task_dead(struct task *t, int status);
void     ptrace_unlink(struct task *t);
int      ptrace_wait(struct task *self, int idtype, uint32_t id, int options, int *status, bool *any);
void     ptrace_switch_dr(struct task *old, struct task *next);
int      ptrace_debug_trap(struct task *t, struct int_frame_t *f, uint64_t *addr);
bool     ptrace_traced(struct task *t);
int      ptrace_access(struct task *t, uintptr_t addr, void *buf, size_t len, bool write);
int64_t  sys_ptrace(uint64_t req, uint64_t pid, uint64_t addr, uint64_t data);

#endif
