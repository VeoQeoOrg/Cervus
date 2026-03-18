#include "../include/sched/sched.h"
#include "../include/sched/capabilities.h"
#include "../include/sched/spinlock.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/io/serial.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/apic/apic.h"
#include "../include/gdt/gdt.h"
#include "../include/fs/vfs.h"
#include <string.h>
#include <stdlib.h>

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 0x1000)
#define MAX_PIDS 1024

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[MAX_CPUS] = {0};

static task_t* percpu_ready_queues[MAX_CPUS][MAX_PRIORITY + 1] = {0};
static task_t  idle_tasks[MAX_CPUS];
static task_t  bootstrap_tasks[MAX_CPUS];
static volatile uint64_t reschedule_calls = 0;
static spinlock_t ready_queue_lock = SPINLOCK_INIT;
static task_t*    pid_table[MAX_PIDS] = {0};
static uint32_t   next_pid = 1;
static spinlock_t pid_lock = SPINLOCK_INIT;

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
    for (uint32_t i = next_pid; i < MAX_PIDS; i++) {
        if (!pid_table[i]) { next_pid = i + 1; found = i; break; }
    }
    if (!found) {
        for (uint32_t i = 1; i < next_pid; i++) {
            if (!pid_table[i]) { next_pid = i + 1; found = i; break; }
        }
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

static uint64_t alloc_and_init_stack(task_t* t) {
    uintptr_t stack_virt = (uintptr_t)pmm_alloc(KERNEL_STACK_PAGES);
    if (!stack_virt) return 0;
    t->stack_base = stack_virt;
    uintptr_t stack_phys = pmm_virt_to_phys((void*)stack_virt);

    for (size_t i = 0; i < KERNEL_STACK_PAGES; i++) {
        vmm_map_page(vmm_get_kernel_pagemap(),
                     stack_virt + i * 0x1000,
                     stack_phys + i * 0x1000,
                     VMM_PRESENT | VMM_WRITE);
    }
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

    uintptr_t stack_top = (stack_virt + KERNEL_STACK_SIZE) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;
    sp -= 32;
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
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    return (uint64_t)sp;
}

static void enqueue_global(task_t* t) {
    uint64_t f = spinlock_acquire_irqsave(&ready_queue_lock);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;
    spinlock_release_irqrestore(&ready_queue_lock, f);
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
            serial_printf("[SCHED] FATAL: no idle stack CPU %u\n", i);
            while (1) asm volatile("cli; hlt");
        }
        current_task[i] = NULL;
        for (int p = 0; p <= MAX_PRIORITY; p++)
            percpu_ready_queues[i][p] = NULL;
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
    serial_printf("[SCHED] task_create: '%s' pid=%u prio=%d\n", t->name, t->pid, t->priority);
    return t;
}

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid, uint32_t gid) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
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

    pid_register(t);
    enqueue_global(t);
    serial_printf("[SCHED] task_create_user: '%s' pid=%u uid=%u entry=0x%llx user_rsp=0x%llx caps=0x%llx\n",
                  t->name, t->pid, t->uid, entry, user_rsp, t->capabilities);
    return t;
}

task_t* task_fork(task_t* parent) {
    if (!parent) return NULL;
    task_t* child = calloc(1, sizeof(task_t));
    if (!child) return NULL;
    child->pid = task_alloc_pid();
    if (!child->pid) { free(child); return NULL; }
    child->ppid            = parent->pid;
    child->priority        = parent->priority;
    child->is_userspace    = parent->is_userspace;
    strncpy(child->name, parent->name, sizeof(child->name)-1);
    child->uid             = parent->uid;
    child->gid             = parent->gid;
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
    serial_printf("[FORK-DBG2] parent pid=%u user_rsp=0x%llx user_saved_rip=0x%llx\n",
                  parent->pid, parent->user_rsp, parent->user_saved_rip);
    child->flags |= TASK_FLAG_FORK;
    atomic_init_bool(&child->on_cpu, false);
    child->rsp = alloc_and_init_stack(child);
    if (!child->rsp) {
        vmm_free_pagemap(child->pagemap);
        free(child);
        return NULL;
    }
    vmm_sync_kernel_mappings(child->pagemap);
    serial_printf("[FORK-DBG] child pid=%u stack_base=0x%llx rsp=0x%llx\n",
                  child->pid, child->stack_base, child->rsp);
    child->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    if (child->fpu_state && parent->fpu_state) {
        memcpy(child->fpu_state, parent->fpu_state, sizeof(fpu_state_t));
        child->fpu_used = parent->fpu_used;
    }
    child->user_saved_rip = parent->user_saved_rip;
    child->user_saved_rbp = parent->user_saved_rbp;
    child->user_saved_rbx = parent->user_saved_rbx;
    child->user_saved_r12 = parent->user_saved_r12;
    child->user_saved_r13 = parent->user_saved_r13;
    child->user_saved_r14 = parent->user_saved_r14;
    child->user_saved_r15 = parent->user_saved_r15;
    child->user_saved_r11 = parent->user_saved_r11;

    if (parent->fd_table)
        child->fd_table = fd_table_clone(parent->fd_table);

    child->state   = TASK_READY;
    child->runnable = true;
    child->parent  = parent;
    child->sibling = parent->children;
    parent->children = child;
    pid_register(child);
    enqueue_global(child);
    serial_printf("[SCHED] fork: parent='%s' pid=%u -> child pid=%u\n",
                  parent->name, parent->pid, child->pid);
    return child;
}

void task_destroy(task_t* task) {
    if (!task) return;
    pid_unregister(task);
    if (task->fpu_state) {
        pmm_free(task->fpu_state, 1);
        task->fpu_state = NULL;
    }

    if (task->flags & TASK_FLAG_STACK_DEFERRED) {
        bool stack_freed_by_timer = true;
        for (uint32_t i = 0; i < MAX_CPUS; i++) {
            if (!percpu_regions[i]) continue;
            void *expected = (void*)task;
            void *prev = __sync_val_compare_and_swap(
                             (void**)&percpu_regions[i]->deferred_free_task,
                             expected, NULL);
            if (prev == expected) {
                stack_freed_by_timer = false;
                uintptr_t sb = task->stack_base;
                task->stack_base = 0;
                asm volatile("" ::: "memory");
                if (sb) pmm_free((void*)sb, KERNEL_STACK_PAGES);
                break;
            }
        }
        (void)stack_freed_by_timer;
    } else if (task->stack_base) {
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
    uint64_t _irqf;
    _irqf = spinlock_acquire_irqsave(&pid_lock);
    for (uint32_t i = 1; i < MAX_PIDS; i++) {
        task_t* t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wait_for_pid != pid && t->wait_for_pid != (uint32_t)-1) continue;
        serial_printf("[SCHED] wakeup_waiters: waking pid=%u (waited for pid=%u) on_cpu=%d\n",
                      t->pid, pid, (int)t->on_cpu._val);
        t->wait_for_pid = 0;
        t->runnable     = true;
        t->state        = TASK_READY;
        spinlock_release_irqrestore(&pid_lock, _irqf);
        enqueue_global(t);
        _irqf = spinlock_acquire_irqsave(&pid_lock);
    }
    spinlock_release_irqrestore(&pid_lock, _irqf);
}

__attribute__((noreturn)) void task_exit(void) {
    uint32_t cpu = lapic_get_id();
    asm volatile("cli");

    percpu_t* pc = get_percpu();
    task_t* me = pc ? (task_t*)pc->current_task : current_task[cpu];

    if (pc) fix_gs_base(pc);

    if (!me || !me->is_userspace) {
        uint64_t gs_base = 0;
        uint64_t kgs_base_lo = 0, kgs_base_hi = 0;
        asm volatile("rdgsbase %0" : "=r"(gs_base));
        asm volatile("mov $0xC0000102, %%ecx; rdmsr"
                     : "=a"(kgs_base_lo), "=d"(kgs_base_hi)
                     :: "ecx");
        uint64_t kgs_base = kgs_base_lo | ((uint64_t)kgs_base_hi << 32);

        serial_printf("[SCHED] task_exit: spurious call on cpu=%u (current='%s')\n"
                      "  pc=0x%llx gs_base=0x%llx kgs_base=0x%llx\n"
                      "  current_task[%u]=%p gs:current_task=%p\n",
                      cpu, me ? me->name : "null",
                      (uint64_t)pc, gs_base, kgs_base,
                      cpu, current_task[cpu],
                      pc ? (void*)pc->current_task : (void*)0xDEAD);

        if (me) {
            me->runnable = false; me->state = TASK_ZOMBIE; me->exit_code = 0;
            current_task[cpu] = NULL;
            if (pc) { pc->current_task = NULL; me->flags |= TASK_FLAG_STACK_DEFERRED; pc->deferred_free_task = (void*)me; }
            task_wakeup_waiters(me->pid);
        }
        sched_reschedule();
        while (1) asm volatile("hlt");
    }

    me->runnable = false;
    me->state    = TASK_ZOMBIE;

    current_task[cpu] = NULL;
    if (pc) {
        pc->current_task = NULL;
        me->flags |= TASK_FLAG_STACK_DEFERRED;
        pc->deferred_free_task = (void*)me;
    }

    task_t* init  = task_find_by_pid(1);
    task_t* child = me->children;
    while (child) {
        task_t* sib = child->sibling;
        if (init && init != me)
            task_reparent(child, init);
        child = sib;
    }

    serial_printf("[SCHED] task_exit: '%s' pid=%u exit=%d cpu=%u\n",
                  me->name, me->pid, me->exit_code, cpu);

    task_wakeup_waiters(me->pid);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());

    sched_reschedule();
    while (1) asm volatile("hlt");
}

void task_kill(task_t* target) {
    if (!target) return;
    uint64_t _irqf;
    asm volatile("cli");
    target->runnable = false;
    target->state    = TASK_ZOMBIE;
    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    task_t* prev = NULL;
    task_t* cur  = ready_queues[target->priority];
    while (cur) {
        if (cur == target) {
            if (prev) prev->next = cur->next;
            else      ready_queues[target->priority] = cur->next;
            break;
        }
        prev = cur; cur = cur->next;
    }
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        prev = NULL;
        cur  = percpu_ready_queues[cpu][target->priority];
        while (cur) {
            if (cur == target) {
                if (prev) prev->next = cur->next;
                else percpu_ready_queues[cpu][target->priority] = cur->next;
                break;
            }
            prev = cur; cur = cur->next;
        }
        if (current_task[cpu] == target) {
            extern uint32_t smp_get_lapic_id_for_cpu(uint32_t);
            ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(cpu));
        }
    }
    asm volatile("sti");
}

static task_t* sched_pick_next(uint32_t cpu) {
    uint64_t _irqf;
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t** head = &percpu_ready_queues[cpu][p];
        task_t*  t    = *head;
        while (t) {
            if (t->runnable) {
                bool expected = false;
                if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                    *head   = t->next;
                    t->next = NULL;
                    return t;
                }
            }
            head = &t->next;
            t    = *head;
        }
    }

    _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
    task_t* found = NULL;
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t** head = &ready_queues[p];
        task_t*  t    = *head;
        while (t) {
            if (t->runnable) {
                bool expected = false;
                if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                    *head   = t->next;
                    t->next = NULL;
                    found   = t;
                    goto done_global;
                }
            }
            head = &t->next;
            t    = *head;
        }
    }
done_global:
    spinlock_release_irqrestore(&ready_queue_lock, _irqf);
    return found ? found : &idle_tasks[cpu];
}

void sched_reschedule(void) {
    uint64_t _irqf;
    asm volatile("cli");

    reschedule_calls++;
    uint32_t cpu  = lapic_get_id();
    task_t*  old  = current_task[cpu];
    task_t*  next = sched_pick_next(cpu);

    if (!next) { asm volatile("sti"); return; }

    if (old == next) {
        if (old == &idle_tasks[cpu]) {
            _irqf = spinlock_acquire_irqsave(&ready_queue_lock);
            task_t* found = NULL;
            for (int p = MAX_PRIORITY; p >= 0 && !found; p--) {
                task_t** head = &ready_queues[p];
                task_t*  t    = *head;
                while (t) {
                    if (t->runnable) {
                        bool expected = false;
                        if (atomic_cas_bool(&t->on_cpu, &expected, true)) {
                            *head   = t->next;
                            t->next = NULL;
                            found   = t;
                            break;
                        }
                    }
                    head = &t->next;
                    t    = *head;
                }
            }
            spinlock_release_irqrestore(&ready_queue_lock, _irqf);
            if (found) next = found;
            else        { asm volatile("sti"); return; }
        } else {
            old->time_slice = old->time_slice_init;
            asm volatile("sti");
            return;
        }
    }

    if (old && old->fpu_state) {
        fpu_save(old->fpu_state);
        old->fpu_used = true;
    }

    if (old && old != &idle_tasks[cpu]) {
        if (old->runnable) {
            old->time_slice = old->time_slice_init;
            old->last_cpu   = cpu;
            old->state      = TASK_READY;
            old->next = percpu_ready_queues[cpu][old->priority];
            percpu_ready_queues[cpu][old->priority] = old;
        } else {

        }
    }

    uint64_t switch_cr3 = 0;
    if (next->cr3 && (!old || old->cr3 != next->cr3)) {
        if (next->pagemap)
            vmm_sync_kernel_mappings(next->pagemap);
        asm volatile("mfence" ::: "memory");
        switch_cr3 = next->cr3;
    }

    if (tss[cpu]) {
        if (next->is_userspace && next->stack_base == 0) {
            serial_printf("[SCHED] *** BUG: pid=%u is_userspace but stack_base=0! "
                          "rsp=0x%llx flags=0x%x ***\n",
                          next->pid, next->rsp, next->flags);
            asm volatile("cli; hlt");
        }
        tss[cpu]->rsp0 = next->stack_base + KERNEL_STACK_SIZE;
        percpu_t* pc = get_percpu();
        if (pc) {
            pc->syscall_kernel_rsp = tss[cpu]->rsp0;
            if (next->is_userspace)
                pc->syscall_user_rsp = next->user_rsp;
        }
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
        if (next->is_userspace) {
            serial_printf("[SCHED] CPU %u: first start '%s' pid=%u entry=0x%llx user_rsp=0x%llx\n",
                          cpu, next->name, next->pid,
                          (uint64_t)next->entry, next->user_rsp);
        }
    }

    {
        percpu_t* _pc = get_percpu();
        uint64_t _krsp = _pc ? _pc->syscall_kernel_rsp : 0;
        uint64_t _tss  = (tss[cpu]) ? tss[cpu]->rsp0 : 0;
        serial_printf("[SCHED] ctx_switch cpu=%u: old=%p(rsp=0x%llx run=%d) -> next=pid=%u(rsp=0x%llx sb=0x%llx) tss_rsp0=0x%llx krsp=0x%llx\n",
                      cpu,
                      (void*)old, old ? old->rsp : 0ULL, old ? (int)old->runnable : -1,
                      next->pid, next->rsp, next->stack_base, _tss, _krsp);

        if (next->stack_base && next->rsp != 0 &&
            (next->rsp < next->stack_base || next->rsp >= next->stack_base + KERNEL_STACK_SIZE)) {
            serial_printf("[SCHED] *** STACK CORRUPTION: pid=%u rsp=0x%llx NOT in [0x%llx, 0x%llx)! ***\n",
                          next->pid, next->rsp, next->stack_base, next->stack_base + KERNEL_STACK_SIZE);
        }
    }
    if (old) context_switch(old, next, &current_task[cpu], switch_cr3);
    else     context_switch(&bootstrap_tasks[cpu], next, &current_task[cpu], switch_cr3);

    asm volatile("sti");
}

void task_yield(void) {
    sched_reschedule();
}

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

static void idle_loop(void* arg) {
    (void)arg;
    while (1) asm volatile("sti; hlt");
}