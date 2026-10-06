#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "capabilities.h"
#include "../memory/vmm.h"
#include "../smp/smp.h"
#include "../signal/signal.h"

#ifndef __ATOMIC_RELAXED
#define __ATOMIC_RELAXED 0
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
#endif

typedef struct { volatile bool _val; } atomic_bool;

static inline void atomic_init_bool(atomic_bool *a, bool v) {
    __atomic_store_n(&a->_val, v, __ATOMIC_RELAXED);
}
static inline bool atomic_load_bool_acq(const atomic_bool *a) {
    return __atomic_load_n(&a->_val, __ATOMIC_ACQUIRE);
}
static inline void atomic_store_bool_rel(atomic_bool *a, bool v) {
    __atomic_store_n(&a->_val, v, __ATOMIC_RELEASE);
}

static inline bool atomic_cas_bool(atomic_bool *a, bool *expected, bool desired) {
    return __atomic_compare_exchange_n(
        &a->_val, expected, desired,
        false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

typedef struct fd_table fd_table_t;

typedef struct {
    uint8_t data[512] __attribute__((aligned(16)));
} fpu_state_t;

typedef enum {
    TASK_RUNNING = 0,
    TASK_READY,
    TASK_BLOCKED,
    TASK_ZOMBIE,
    TASK_DEAD,
} task_state_t;

typedef enum {
    TASK_TYPE_KERNEL    = 0,
    TASK_TYPE_USER      = 1,
} task_type_t;

#define MAX_PRIORITY           31
#define DEFAULT_PRIORITY       16
#define TASK_DEFAULT_TIMESLICE 10

#define RLIMIT_CPU_K        0
#define RLIMIT_FSIZE_K      1
#define RLIMIT_DATA_K       2
#define RLIMIT_STACK_K      3
#define RLIMIT_CORE_K       4
#define RLIMIT_RSS_K        5
#define RLIMIT_NPROC_K      6
#define RLIMIT_NOFILE_K     7
#define RLIMIT_MEMLOCK_K    8
#define RLIMIT_AS_K         9
#define RLIMIT_LOCKS_K      10
#define RLIMIT_SIGPENDING_K 11
#define RLIMIT_MSGQUEUE_K   12
#define RLIMIT_NICE_K       13
#define RLIMIT_RTPRIO_K     14
#define RLIMIT_RTTIME_K     15
#define RLIMIT_COUNT        16
#define RLIM_INF_K          (~0ULL)

typedef struct task {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp_save;
    uint64_t cr3;
    int priority;
    bool runnable;
    uint8_t is_userspace;
    uint8_t _pad0[2];
    uint32_t cpu_id;
    char name[32];

    uint32_t time_slice;
    uint32_t time_slice_init;
    uint8_t _pad1[4];
    uint64_t total_runtime;

    fpu_state_t* fpu_state;
    bool fpu_used;
    uint8_t _pad2[3];
    uint32_t last_cpu;
    uint64_t cpu_affinity;

    void (*entry)(void*);
    void *arg;

    uintptr_t stack_base;
    uint64_t user_rsp;

    task_state_t state;
    uint8_t _pad3[4];

    struct task* next;

    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t gid;
    uint64_t capabilities;

    struct task* parent;
    struct task* children;
    struct task* sibling;

    int exit_code;
    volatile bool pending_kill;
    uint8_t _pad4[3];

    uintptr_t brk_start;
    uintptr_t brk_current;
    uintptr_t brk_max;
    vmm_pagemap_t* pagemap;

    uint32_t flags;
    uint32_t wait_for_pid;

    uint64_t wakeup_time_ns;

    uint64_t user_saved_rip;
    uint64_t user_saved_rbp;
    uint64_t user_saved_rbx;
    uint64_t user_saved_r12;
    uint64_t user_saved_r13;
    uint64_t user_saved_r14;
    uint64_t user_saved_r15;
    uint64_t user_saved_r11;

    fd_table_t      *fd_table;

    atomic_bool on_cpu;
    atomic_bool on_rq;

    int ctty;

    uint64_t run_start_ns;

    char cwd[256];

    uint32_t pgid;
    uint32_t sid;

    uint64_t create_time_ns;

    struct puzzle_process *puzzle;

    uint64_t sig_pending;
    uint64_t sig_blocked;

    uint64_t user_saved_rdi;
    uint64_t user_saved_rsi;
    uint64_t user_saved_rdx;
    uint64_t user_saved_r10;
    uint64_t user_saved_r8;
    uint64_t user_saved_r9;
    uint64_t user_saved_rax;

    uint64_t  fs_base;
    uintptr_t tls_vaddr;
    uint64_t  tls_filesz;
    uint64_t  tls_memsz;
    uint64_t  tls_align;

    uint32_t  umask;
    uint64_t  alarm_at_ns;
    uint64_t  alarm_interval_ns;

    struct task_mapping *mappings;
    int io_nonblock;

    volatile bool in_syscall;
    uint64_t cur_syscall;
    uint64_t cur_syscall_args[6];
    uint64_t sig_saved_mask;
    uint8_t  sig_saved_valid;
    char     exe[256];
    struct mm *mm;
    uint64_t  *prof_buf;
    uint32_t   prof_n;
    char      *at_base;
    uint64_t   user_saved_rcx;
    uint32_t   ret_iret;
    uint32_t   ss_flags;
    uint64_t   ss_sp;
    uint64_t   ss_size;
    uint64_t   user_iret_r11;
    int32_t    term_sig;
    int32_t    kill_sig;
    struct {
        int32_t  code;
        uint32_t pid;
        uint32_t uid;
        int32_t  status;
        uint64_t addr;
        int32_t  trapno;
        int32_t  err;
        uint64_t value;
    } sig_si[NSIG];
    uint64_t   sig_handler[NSIG];
    uint64_t   sig_flags[NSIG];
    uint64_t   sig_restorer[NSIG];
    uint64_t   sig_mask[NSIG];
    uint64_t   sig_waitset;
    uint32_t   rtq_n;
    uint32_t   rtq_pad;
    struct {
        int32_t  sig;
        int32_t  code;
        uint32_t pid;
        uint32_t uid;
        uint64_t value;
    } rtq[SIGQUEUE_MAX];
    uint64_t   maxrss_bytes;
    uint64_t   child_runtime_ns;
    uint64_t   child_maxrss_bytes;
    int32_t    nice;
    uint32_t   xcpu_sent;
    uint8_t    policy;
    uint8_t    rt_prio;
    uint16_t   policy_pad;
    struct {
        uint64_t cur;
        uint64_t max;
    } rlim[RLIMIT_COUNT];
} task_t;

#define TASK_FLAG_TRACE          (1 << 0)
#define TASK_FLAG_VFORK          (1 << 1)
#define TASK_FLAG_FORK           (1 << 2)
#define TASK_FLAG_STARTED        (1 << 3)
#define TASK_FLAG_OWN_PAGEMAP    (1 << 4)
#define TASK_FLAG_STACK_DEFERRED (1 << 5)
#define TASK_FLAG_DESTROYED      (1U << 31)

_Static_assert(offsetof(task_t, rsp)            ==   0, "task_t: rsp");
_Static_assert(offsetof(task_t, entry)          == 120, "task_t: entry — update TASK_ENTRY_OFFSET");
_Static_assert(offsetof(task_t, arg)            == 128, "task_t: arg   — update TASK_ARG_OFFSET");
_Static_assert(offsetof(task_t, stack_base)     == 136, "task_t: stack_base");
_Static_assert(offsetof(task_t, user_rsp)       == 144, "task_t: user_rsp — update TASK_USER_RSP_OFFSET");
_Static_assert(offsetof(task_t, user_saved_rip) == 272, "task_t: user_saved_rip");
_Static_assert(offsetof(task_t, user_saved_rbp) == 280, "task_t: user_saved_rbp — update TASK_USER_SAVED_RBP_OFFSET");
_Static_assert(offsetof(task_t, cr3)            ==  24, "task_t: cr3 — update TASK_CR3_OFFSET");
_Static_assert(offsetof(task_t, user_saved_rbx) == 288, "task_t: user_saved_rbx");
_Static_assert(offsetof(task_t, user_saved_r12) == 296, "task_t: user_saved_r12");
_Static_assert(offsetof(task_t, user_saved_r13) == 304, "task_t: user_saved_r13");
_Static_assert(offsetof(task_t, user_saved_r14) == 312, "task_t: user_saved_r14");
_Static_assert(offsetof(task_t, user_saved_r15) == 320, "task_t: user_saved_r15");
_Static_assert(offsetof(task_t, user_saved_r11) == 328, "task_t: user_saved_r11");
_Static_assert(offsetof(task_t, on_cpu)         == 0x158, "task_t: on_cpu — update TASK_ON_CPU_OFFSET");
_Static_assert(offsetof(task_t, user_saved_rdi) == 656, "task_t: user_saved_rdi — update syscall_asm.asm");
_Static_assert(offsetof(task_t, user_saved_rsi) == 664, "task_t: user_saved_rsi");
_Static_assert(offsetof(task_t, user_saved_rdx) == 672, "task_t: user_saved_rdx");
_Static_assert(offsetof(task_t, user_saved_r10) == 680, "task_t: user_saved_r10");
_Static_assert(offsetof(task_t, user_saved_r8)  == 688, "task_t: user_saved_r8");
_Static_assert(offsetof(task_t, user_saved_r9)  == 696, "task_t: user_saved_r9");
_Static_assert(offsetof(task_t, user_saved_rax) == 704, "task_t: user_saved_rax");
_Static_assert(offsetof(task_t, user_saved_rcx) == 1152, "task_t: user_saved_rcx — update syscall_asm.asm");
_Static_assert(offsetof(task_t, ret_iret)       == 1160, "task_t: ret_iret — update syscall_asm.asm");
_Static_assert(offsetof(task_t, user_iret_r11)  == 1184, "task_t: user_iret_r11 — update syscall_asm.asm");

extern task_t* ready_queues[MAX_PRIORITY + 1];
extern task_t* current_task[MAX_CPUS];

void sched_init(void);
void sched_reschedule(void);
void sched_print_stats(void);
void task_yield(void);
void profile_tick(task_t *t, uint64_t rip);
void profile_start(task_t *t);
void profile_finish(task_t *t);

struct vnode;
int  task_map_track(task_t *t, struct vnode *vn);
void task_map_drop_all(task_t *t);
void task_sleep_ns(uint64_t ns);
void task_sleep_ms(uint64_t ms);
uint64_t task_set_alarm(task_t *t, uint64_t seconds);
void     task_set_itimer(task_t *t, uint64_t value_ns, uint64_t interval_ns,
                         uint64_t *old_value_ns, uint64_t *old_interval_ns);
void sched_note_wakeup(uint64_t deadline_ns);
void task_sample_rss(task_t *t);
void task_rlimits_default(task_t *t);
void task_inherit_limits(task_t *dst, const task_t *src);
void task_apply_nice(task_t *t);
void task_apply_policy(task_t *t);

#define SCHED_OTHER_K 0
#define SCHED_FIFO_K  1
#define SCHED_RR_K    2
#define SCHED_BATCH_K 3
#define SCHED_IDLE_K  5
uint64_t sched_current_rlimit(int res);
void sched_signal_current(int sig);
int task_count_uid(uint32_t uid);
void sched_check_cpu_limit(task_t *t, uint64_t now_ns);

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority);
task_t* task_create_ex(const char* name, void (*entry)(void*), void* arg, int priority, uint64_t affinity);
int     sched_find_free_cpu(void);
int     sched_find_free_cpu_mask(uint64_t exclude);
void    spec_selftest(void);

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid, uint32_t gid);

__attribute__((noreturn)) void task_exit(void);
void    task_kill(task_t* task);
void    task_kill_subtree(task_t* root);
void    task_destroy(task_t* task);
task_t* task_fork(task_t* parent);
task_t* task_find_by_pid(uint32_t pid);
int task_collect_pids(uint32_t *out, int max);
uint32_t task_alloc_pid(void);
void    task_reparent(task_t* child, task_t* new_parent);

#include "spinlock.h"
extern spinlock_t children_lock;
void    task_wakeup_waiters(uint32_t pid);
void    task_unblock(task_t* t);
void    sched_wakeup_sleepers(uint64_t now_ns);
task_t* task_find_foreground(void);
void task_set_foreground(uint32_t pid);
void task_clear_foreground_if(uint32_t pid);

extern void context_switch(task_t* old, task_t* next, task_t** current_task_slot, uint64_t new_cr3);
extern void first_task_start(task_t* task);
extern void task_trampoline(void);
extern void task_trampoline_user(void);
extern void task_trampoline_fork(void);
extern void fpu_save(fpu_state_t* state);
extern void fpu_restore(fpu_state_t* state);
#endif
