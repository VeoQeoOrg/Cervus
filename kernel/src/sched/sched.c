#include "../include/sched/capabilities.h"
#include "../include/sched/sched.h"
#include "../include/sched/spinlock.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/io/serial.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/apic/apic.h"
#include "../include/gdt/gdt.h"
#include "../include/fs/vfs.h"
#include "../include/panic/panic.h"
#include "../include/console/console.h"
#include <string.h>
#include <stdlib.h>

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 0x1000)
#define MAX_PIDS 4096

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[MAX_CPUS] = {0};

static task_t  idle_tasks[MAX_CPUS];
static task_t  bootstrap_tasks[MAX_CPUS];
static volatile uint64_t reschedule_calls = 0;
static spinlock_t ready_queue_lock = SPINLOCK_INIT;
static task_t*    pid_table[MAX_PIDS] = {0};
static uint32_t   next_pid = 1;
static spinlock_t pid_lock = SPINLOCK_INIT;

spinlock_t children_lock = SPINLOCK_INIT;

static volatile uint64_t g_earliest_wakeup_ns = UINT64_MAX;

void sched_note_wakeup(uint64_t deadline_ns) {
    if (deadline_ns == 0) return;
    uint64_t cur = __atomic_load_n(&g_earliest_wakeup_ns, __ATOMIC_RELAXED);
    while (deadline_ns < cur) {
        if (__atomic_compare_exchange_n(&g_earliest_wakeup_ns, &cur, deadline_ns,
                                        false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            break;
    }
}

extern tss_t* tss[MAX_CPUS];

static inline void fix_gs_base(percpu_t* pc) {
    uint64_t val = (uint64_t)pc;
    asm volatile("wrmsr"
                 :: "c"(0xC0000101U),
                    "a"((uint32_t)val),
                    "d"((uint32_t)(val >> 32)));
}

uint32_t task_alloc_pid(void) {
    uint64_t _irqf;
    _irqf = spinlock_acquire_irqsave(&pid_lock);
    uint32_t found = 0;
    for (uint32_t attempt = 0; attempt < MAX_PIDS - 1; attempt++) {
        uint32_t i = next_pid;
        if (i == 0 || i >= MAX_PIDS) {
            next_pid = 1;
            i = 1;
        }
        next_pid = (next_pid + 1 >= MAX_PIDS) ? 1 : next_pid + 1;
        if (!pid_table[i]) { found = i; break; }
    }
    if (!found) {
        serial_printf("[PID] FATAL: pid table exhausted (MAX_PIDS=%u)!\n", MAX_PIDS);
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);
    return found;
}

task_t* task_find_by_pid(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PIDS) return NULL;
    return pid_table[pid];
}

static void pid_register(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = t;
}

static void pid_unregister(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = NULL;
}

static void idle_loop(void* arg);

#define STACK_CANARY_VALUE  0xDEADC0DEDEADC0DEULL

static uint64_t alloc_and_init_stack(task_t* t) {
    uintptr_t stack_virt = (uintptr_t)pmm_alloc_zero(KERNEL_STACK_PAGES);
    if (!stack_virt) return 0;
    t->stack_base = stack_virt;

    uint64_t* canary_page = (uint64_t*)stack_virt;
    for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++)
        canary_page[i] = STACK_CANARY_VALUE;

    uintptr_t stack_top = (stack_virt + KERNEL_STACK_SIZE) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;

    if (t->flags & TASK_FLAG_FORK) {
        extern void task_trampoline_fork(void);
        *--sp = (uint64_t)task_trampoline_fork;
    } else if (t->is_userspace) {
        extern void task_trampoline_user(void);
        *--sp = (uint64_t)task_trampoline_user;
    } else {
        extern void task_trampoline(void);
        *--sp = (uint64_t)task_trampoline;
    }

    *--sp = (uint64_t)t;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    return (uint64_t)sp;
}

static void enqueue_global(task_t* t) {
    uint64_t f = spinlock_acquire_irqsave(&ready_queue_lock);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;
    spinlock_release_irqrestore(&ready_queue_lock, f);
}

void __attribute__((used)) ctx_rsp_corruption_detected(task_t* old, uint64_t saved_rsp) {
    serial_printf("[CTX-CORRUPT] pid=%u rsp=0x%llx saved but INVALID (stack=0x%llx..0x%llx)!\n",
                  old ? old->pid : 0,
                  saved_rsp,
                  old ? old->stack_base : 0,
                  old ? (old->stack_base + KERNEL_STACK_SIZE) : 0);
    kernel_panic("context_switch: saved invalid RSP into task->rsp");
}

static void process_deferred_free(void) {
    percpu_t* pc = get_percpu();
    if (!pc) return;
    task_t* dead = (task_t*)pc->deferred_free_task;
    if (!dead) return;
    pc->deferred_free_task = NULL;
    task_destroy(dead);
}

void sched_init(void) {
    memset(pid_table, 0, sizeof(pid_table));
    memset(bootstrap_tasks, 0, sizeof(bootstrap_tasks));
    next_pid = 1;
    for (uint32_t i = 0; i < smp_get_cpu_count(); i++) {
        task_t* idle = &idle_tasks[i];
        memset(idle, 0, sizeof(task_t));
        idle->priority        = 0;
        idle->runnable        = true;
        idle->state           = TASK_READY;
        idle->cpu_id          = i;
        idle->last_cpu        = i;
        idle->time_slice      = 1;
        idle->time_slice_init = 1;
        idle->entry           = idle_loop;
        idle->arg             = NULL;
        idle->is_userspace    = TASK_TYPE_KERNEL;
        idle->pid             = 0;
        idle->uid             = UID_ROOT;
        idle->gid             = GID_ROOT;
        idle->capabilities    = CAP_ALL;
        atomic_init_bool(&idle->on_cpu, false);
        idle->name[0]='i'; idle->name[1]='d';
        idle->name[2]='l'; idle->name[3]='e';
        idle->rsp = alloc_and_init_stack(idle);
        if (!idle->rsp) {
            kernel_panic("SCHED: failed to allocate idle stack");
        }
        current_task[i] = NULL;
    }
    serial_writestring("Scheduler initialized (PREEMPTIVE SMP MODE)\n");
}

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->uid             = UID_ROOT;
    t->gid             = GID_ROOT;
    t->capabilities    = CAP_ALL;
    t->entry           = entry;
    t->arg             = arg;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = (uint64_t)entry;
    t->is_userspace    = TASK_TYPE_KERNEL;
    atomic_init_bool(&t->on_cpu, false);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    pid_register(t);
    enqueue_global(t);
    return t;
}

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid, uint32_t gid) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->pgid            = t->pid;
    t->sid             = t->pid;
    t->uid             = uid;
    t->gid             = gid;
    t->capabilities    = cap_initial(uid);
    t->entry           = (void (*)(void*))entry;
    t->arg             = NULL;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = entry;
    t->is_userspace    = TASK_TYPE_USER;
    t->user_rsp        = user_rsp;
    t->cr3             = cr3;
    t->pagemap         = pagemap;
    t->brk_start       = 0;
    t->brk_current     = 0;
    t->brk_max         = 0x0000700000000000ULL;
    atomic_init_bool(&t->on_cpu, false);
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->cwd[0] = '/'; t->cwd[1] = '\0';
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);

    t->fd_table = fd_table_create();
    if (t->fd_table) {
        int stdio_ret = vfs_init_stdio(t);
        if (stdio_ret < 0)
            serial_printf("[SCHED] task_create_user: vfs_init_stdio failed: %d\n",
                          stdio_ret);
    }

    t->flags |= TASK_FLAG_OWN_PAGEMAP;

    pid_register(t);
    enqueue_global(t);
    return t;
}

task_t* task_fork(task_t* parent) {
    if (!parent) return NULL;
    task_t* child = calloc(1, sizeof(task_t));
    if (!child) return NULL;
    child->pid = task_alloc_pid();
    if (!child->pid) { free(child); return NULL; }
    child->ppid            = parent->pid;
    child->pgid            = parent->pgid;
    child->sid             = parent->sid;
    child->priority        = parent->priority;
    child->is_userspace    = parent->is_userspace;
    strncpy(child->name, parent->name, sizeof(child->name)-1);
    child->uid             = parent->uid;
    child->gid             = parent->gid;
    child->ctty            = parent->ctty;
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->capabilities    = parent->capabilities;
    child->time_slice      = parent->time_slice_init;
    child->time_slice_init = parent->time_slice_init;
    child->pagemap = vmm_clone_pagemap(parent->pagemap);
    if (!child->pagemap) { free(child); return NULL; }
    child->cr3 = (uint64_t)pmm_virt_to_phys(child->pagemap->pml4);
    child->brk_start   = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->brk_max     = parent->brk_max;
    child->user_rsp = parent->user_rsp;

    child->user_saved_rip = parent->user_saved_rip;
    child->user_saved_rbp = parent->user_saved_rbp;
    child->user_saved_rbx = parent->user_saved_rbx;
    child->user_saved_r12 = parent->user_saved_r12;
    child->user_saved_r13 = parent->user_saved_r13;
    child->user_saved_r14 = parent->user_saved_r14;
    child->user_saved_r15 = parent->user_saved_r15;
    child->user_saved_r11 = parent->user_saved_r11 | (1ULL << 9);

    child->flags |= TASK_FLAG_FORK;
    atomic_init_bool(&child->on_cpu, false);
    child->rsp = alloc_and_init_stack(child);
    if (!child->rsp) {
        vmm_free_pagemap(child->pagemap);
        free(child);
        return NULL;
    }
    vmm_sync_kernel_mappings(child->pagemap);
    child->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    if (child->fpu_state && parent->fpu_state) {
        memcpy(child->fpu_state, parent->fpu_state, sizeof(fpu_state_t));
        child->fpu_used = parent->fpu_used;
    }

    if (parent->fd_table)
        child->fd_table = fd_table_clone(parent->fd_table);

    child->state    = TASK_READY;
    child->runnable = true;
    child->parent   = parent;

    {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        child->sibling   = parent->children;
        parent->children = child;
        spinlock_release_irqrestore(&children_lock, _cf);
    }

    pid_register(child);

    LOG_D("[FORK-CHK] child=%p rsp=0x%llx stk=0x%llx rip=0x%llx\n",
                  (void*)child, child->rsp, child->stack_base, child->user_saved_rip);

    enqueue_global(child);
    return child;
}

void task_destroy(task_t* task) {
    if (!task) return;

    uint32_t old_flags = __atomic_fetch_or(&task->flags, TASK_FLAG_DESTROYED, __ATOMIC_ACQ_REL);
    if (old_flags & TASK_FLAG_DESTROYED) return;

    LOG_D("[DESTROY] pid=%u flags=0x%x on_cpu=%d\n",
                  task->pid, task->flags, (int)atomic_load_bool_acq(&task->on_cpu));

    pid_unregister(task);

    if (task->fpu_state) {
        pmm_free(task->fpu_state, 1);
        task->fpu_state = NULL;
    }

    if (task->stack_base) {
        pmm_free((void*)task->stack_base, KERNEL_STACK_PAGES);
        task->stack_base = 0;
    }

    if (task->pagemap && (task->flags & (TASK_FLAG_FORK | TASK_FLAG_OWN_PAGEMAP))) {
        vmm_free_pagemap(task->pagemap);
        task->pagemap = NULL;
    }

    if (task->fd_table) {
        fd_table_destroy(task->fd_table);
        task->fd_table = NULL;
    }

    free(task);
}

void task_reparent(task_t* child, task_t* new_parent) {
    if (!child || !new_parent) return;
    child->parent  = new_parent;
    child->ppid    = new_parent->pid;
    child->sibling = new_parent->children;
    new_parent->children = child;
}

void task_wakeup_waiters(uint32_t pid) {
    task_t* to_wake[64];
    int wake_count = 0;

    uint64_t _irqf = spinlock_acquire_irqsave(&pid_lock);
    for (uint32_t i = 1; i < MAX_PIDS && wake_count < 64; i++) {
        task_t* t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wait_for_pid != pid && t->wait_for_pid != (uint32_t)-1) continue;

        LOG_D("[SCHED] wakeup_waiters: waking pid=%u (waited for pid=%u)\n", t->pid, pid);

        t->wait_for_pid = 0;
        t->runnable = true;
        t->state = TASK_READY;

        to_wake[wake_count++] = t;
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);

    for (int i = 0; i < wake_count; i++)
        enqueue_global(to_wake[i]);
}

extern void tty_reset_nonblock(void);
extern void tty_reset_on_exit(void);

__attribute__((noreturn)) void task_exit(void)
{
    asm volatile("cli");
    uint32_t cpu = lapic_get_id();
    percpu_t* pc = get_percpu();
    task_t* me = pc ? (task_t*)pc->current_task : current_task[cpu];

    if (!me) kernel_panic("task_exit: no current task");

    LOG_D("[EXIT] task_exit called cpu=%u me=%p pid=%u\n", cpu, (void*)me, me->pid);

    tty_reset_on_exit();

    task_t* init = task_find_by_pid(1);
    if (init && init != me) {
        uint64_t _cf = spinlock_acquire_irqsave(&children_lock);
        task_t* child = me->children;
        me->children = NULL;
        while (child) {
            task_t* sib = child->sibling;
            task_reparent(child, init);
            child = sib;
        }
        spinlock_release_irqrestore(&children_lock, _cf);
    }

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    me->runnable = false;
    me->state = TASK_ZOMBIE;
    me->cr3 = 0;

    task_wakeup_waiters(me->pid);

    sched_reschedule();

    kernel_panic("task_exit: returned from sched_reschedule (should never happen)");
}

void task_kill(task_t* target) {
    if (!target) return;

    if (target->pid == 0) return;
    if (target->is_userspace == TASK_TYPE_KERNEL) return;

    if (target->state == TASK_ZOMBIE || target->state == TASK_DEAD) return;
    if (target->pending_kill) return;

    LOG_D("[KILL] task_kill pid=%u state=%d cpu=%u\n",
                  target->pid, (int)target->state, lapic_get_id());
    target->exit_code = 130;
    target->pending_kill = true;

    if (target->state == TASK_BLOCKED) {
        target->wakeup_time_ns = 0;
        task_unblock(target);
    }

    if (!(target->flags & TASK_FLAG_STARTED)) {
        return;
    }

    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        if (current_task[cpu] == target) {
            extern uint32_t smp_get_lapic_id_for_cpu(uint32_t);
            ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(cpu));
        }
    }
}

static volatile uint32_t g_foreground_pid[VT_COUNT];

static int caller_ctty(void) {
    percpu_t *pc = get_percpu();
    task_t *me = pc ? (task_t *)pc->current_task : current_task[lapic_get_id()];
    int vt = me ? me->ctty : 0;
    if (vt < 0 || vt >= VT_COUNT) vt = 0;
    return vt;
}

void task_set_foreground(uint32_t pid) {
    g_foreground_pid[caller_ctty()] = pid;
}

void task_clear_foreground_if(uint32_t pid) {
    int vt = caller_ctty();
    if (g_foreground_pid[vt] == pid) g_foreground_pid[vt] = 0;
}

task_t* task_find_foreground(void) {
    int vt = vt_active();
    if (vt < 0 || vt >= VT_COUNT) return NULL;
    uint32_t fpid = g_foreground_pid[vt];
    if (fpid == 0) return NULL;
    task_t *t = task_find_by_pid(fpid);
    if (!t || t->state == TASK_ZOMBIE || t->state == TASK_DEAD) {
        g_foreground_pid[vt] = 0;
        return NULL;
    }
    return t;
}

static task_t* sched_pick_next(uint32_t cpu) {
    uint64_t _irqf;

    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    task_t* found = NULL;
    for (int p = MAX_PRIORITY; p >= 0 && !found; p--) {
        task_t** head = &ready_queues[p];
        task_t*  t    = *head;
        while (t) {
            if (t->runnable && t->state != TASK_ZOMBIE && t->state != TASK_DEAD) {
                bool expected = false;
                if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                    *head   = t->next;
                    t->next = NULL;
                    found   = t;
                    break;
                }
            } else {
                if (t->state == TASK_ZOMBIE || t->state == TASK_DEAD || !t->runnable) {
                    *head = t->next;
                    t->next = NULL;
                    t = *head;
                    continue;
                }
            }
            head = &t->next;
            t    = *head;
        }
    }
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
    return found ? found : &idle_tasks[cpu];
}

void sched_reschedule(void) {
    asm volatile("cli");

    process_deferred_free();

    reschedule_calls++;
    uint32_t cpu  = lapic_get_id();

    task_t*  old  = current_task[cpu];
    task_t*  next = sched_pick_next(cpu);

    if (!next) { asm volatile("sti"); return; }

    if (old == next) {
        if (old == &idle_tasks[cpu]) {
            next = sched_pick_next(cpu);
            if (next == &idle_tasks[cpu]) {
                asm volatile("sti");
                return;
            }
        } else {
            old->time_slice = old->time_slice_init;
            asm volatile("sti");
            return;
        }
    }

    if (old && old->fpu_state && old->state != TASK_ZOMBIE && old->state != TASK_DEAD) {
        fpu_save(old->fpu_state);
        old->fpu_used = true;
    }

    if (old && old != &idle_tasks[cpu]) {
        if (old->state == TASK_ZOMBIE) {
            percpu_t* pc = get_percpu();
            if (pc) pc->deferred_free_task = old;
        } else if (old->runnable && old->state != TASK_DEAD) {
            old->time_slice = old->time_slice_init;
            old->last_cpu   = cpu;
            old->state      = TASK_READY;
            enqueue_global(old);
        }
    }

    uint64_t switch_cr3 = 0;
    if (next->cr3 && (!old || old->cr3 != next->cr3)) {
        if (next->pagemap)
            vmm_sync_kernel_mappings(next->pagemap);
        asm volatile("lock addl $0, (%%rsp)" ::: "memory", "cc");
        switch_cr3 = next->cr3;
    } else if (!next->cr3) {
        vmm_pagemap_t* kpm = vmm_get_kernel_pagemap();
        if (kpm && kpm->pml4) {
            uint64_t kphys = (uint64_t)pmm_virt_to_phys(kpm->pml4);
            if (!old || old->cr3 != kphys)
                switch_cr3 = kphys;
        }
    }

    if (tss[cpu]) {
        if (next->is_userspace && next->stack_base == 0) {
            kernel_panic("SCHED: userspace task has stack_base=0");
        }
        tss[cpu]->rsp0 = next->stack_base + KERNEL_STACK_SIZE;
        percpu_t* pc = get_percpu();
        if (pc) {
            pc->syscall_kernel_rsp = tss[cpu]->rsp0;
            if (next->is_userspace) {
                pc->syscall_user_rsp = next->user_rsp;
            }
        }
    }

    if (hpet_is_available()) {
        uint64_t now = hpet_elapsed_ns();
        if (old && old != &idle_tasks[cpu] && old->run_start_ns > 0 && now > old->run_start_ns)
            old->total_runtime += (now - old->run_start_ns);
        next->run_start_ns = now;
    }

    next->cpu_id = cpu;
    next->state  = TASK_RUNNING;
    if (next->fpu_state) fpu_restore(next->fpu_state);

    if (!(next->flags & TASK_FLAG_STARTED)) {
        next->flags |= TASK_FLAG_STARTED;
        current_task[cpu] = next;
        if (next->cr3) {
            asm volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
            switch_cr3 = 0;
        } else {
            asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
        }
    }

    if (old) context_switch(old, next, &current_task[cpu], switch_cr3);
    else     context_switch(&bootstrap_tasks[cpu], next, &current_task[cpu], switch_cr3);

    process_deferred_free();

    asm volatile("sti");
}

void task_yield(void) {
    sched_reschedule();
}

void task_sleep_ns(uint64_t ns) {
    if (ns == 0) return;
    if (!hpet_is_available()) { task_yield(); return; }
    uint32_t cpu = lapic_get_id();
    task_t *me = current_task[cpu];
    if (!me) return;
    me->wakeup_time_ns = hpet_elapsed_ns() + ns;
    sched_note_wakeup(me->wakeup_time_ns);
    me->runnable = false;
    me->state    = TASK_BLOCKED;
    sched_reschedule();
}

void task_sleep_ms(uint64_t ms) { task_sleep_ns(ms * 1000000ULL); }

void sched_print_stats(void) {
    uint64_t _irqf;
    serial_printf("[SCHED] reschedule_calls=%llu\n", reschedule_calls);
    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        int n = 0;
        for (task_t* t = ready_queues[p]; t; t = t->next) n++;
        if (n) serial_printf("  prio %d: %d tasks\n", p, n);
    }
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
}

void task_unblock(task_t* t) {
    if (!t) return;
    t->runnable = true;
    t->state    = TASK_READY;
    enqueue_global(t);
}

void sched_wakeup_sleepers(uint64_t now_ns) {
    if (now_ns < __atomic_load_n(&g_earliest_wakeup_ns, __ATOMIC_RELAXED))
        return;

    task_t* to_wake[64];
    int     wake_count = 0;
    uint64_t next_earliest = UINT64_MAX;

    uint64_t _irqf = spinlock_acquire_irqsave(&pid_lock);
    for (uint32_t i = 1; i < MAX_PIDS; i++) {
        task_t *t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wakeup_time_ns == 0) continue;
        if (now_ns >= t->wakeup_time_ns) {
            if (wake_count < 64) {
                t->wakeup_time_ns = 0;
                t->runnable = true;
                t->state    = TASK_READY;
                to_wake[wake_count++] = t;
            } else {
                if (t->wakeup_time_ns < next_earliest)
                    next_earliest = t->wakeup_time_ns;
            }
        } else if (t->wakeup_time_ns < next_earliest) {
            next_earliest = t->wakeup_time_ns;
        }
    }
    __atomic_store_n(&g_earliest_wakeup_ns, next_earliest, __ATOMIC_RELAXED);
    spinlock_release_irqrestore(&pid_lock, _irqf);

    for (int i = 0; i < wake_count; i++) {
        enqueue_global(to_wake[i]);
    }
}

static void idle_loop(void* arg) {
    (void)arg;
    uint32_t cpu = lapic_get_id();
    serial_printf("[IDLE] CPU %u entering idle loop\n", cpu);
    while (1) {
        process_deferred_free();
        asm volatile("sti; hlt");
    }
}