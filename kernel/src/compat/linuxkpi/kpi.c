#include "../../../include/memory/pmm.h"
#include "../../../include/memory/dma.h"
#include "../../../include/sched/sched.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/smp/smp.h"
#include "../../../include/drivers/random.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/fs/vfs.h"
#include <stdarg.h>
#include <stdio.h>

static inline void *sys_alloc(size_t n)  { return kmalloc(n); }
static inline void *sys_zalloc(size_t n) { return kzalloc(n); }
static inline void  sys_free(void *p)    { kfree(p); }
static inline void *sys_dma_alloc(size_t n, uintptr_t *phys) { return dma_alloc_coherent_low(n, phys); }
static inline void  sys_dma_free(void *v, size_t n) { dma_free_coherent(v, n); }

#include "kpi.h"

#define KPI_THREAD_PRIO 18
#define KPI_WQ_PRIO     16

static struct { u32 depth; u32 saved_if; } g_irq_nest[MAX_CPUS];

void kpi_irq_push(void)
{
    uint64_t rf;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rf) :: "memory");
    u32 cpu = smp_cpu_index();
    if (cpu >= MAX_CPUS) cpu = 0;
    if (g_irq_nest[cpu].depth++ == 0) g_irq_nest[cpu].saved_if = (rf & 0x200) != 0;
}

void kpi_irq_pop(void)
{
    u32 cpu = smp_cpu_index();
    if (cpu >= MAX_CPUS) cpu = 0;
    if (!g_irq_nest[cpu].depth) return;
    if (--g_irq_nest[cpu].depth == 0 && g_irq_nest[cpu].saved_if)
        __asm__ volatile("sti" ::: "memory");
}

void *kpi_kmalloc(size_t size, gfp_t flags)
{
    void *p = sys_alloc(size ? size : 1);
    if (p && (flags & __GFP_ZERO)) memset(p, 0, size);
    return p;
}

void *kpi_kzalloc(size_t size, gfp_t flags)
{
    (void)flags;
    return sys_zalloc(size ? size : 1);
}

void kpi_kfree(const void *p)
{
    if (p) sys_free((void *)p);
}

static const char *const g_lvl_tag[] = { "error", "warning", "", "debug" };

void kpi_log(int level, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_level_t l = level == KPI_ERR ? LOG_LEVEL_ERR : level == KPI_WARN ? LOG_LEVEL_WARN :
                    level == KPI_INFO ? LOG_LEVEL_INFO : LOG_LEVEL_DEBUG;
    size_t n = strlen(buf);
    const char *nl = (n && buf[n - 1] == '\n') ? "" : "\n";
    const char *tag = (level >= 0 && level <= KPI_DBG) ? g_lvl_tag[level] : "";
    if (*tag) serial_printf_lvl(l, "rtw88: %s: %s%s", tag, buf, nl);
    else      serial_printf_lvl(l, "rtw88: %s%s", buf, nl);
}

void kpi_print_hex_dump(const char *prefix, const void *buf, size_t len)
{
    const u8 *p = buf;
    char line[80];
    for (size_t off = 0; off < len; off += 16) {
        int k = snprintf(line, sizeof(line), "%s%04zx:", prefix ? prefix : "", off);
        for (size_t i = off; i < off + 16 && i < len && k < (int)sizeof(line) - 4; i++)
            k += snprintf(line + k, sizeof(line) - (size_t)k, " %02x", p[i]);
        serial_printf_lvl(LOG_LEVEL_DEBUG, "%s\n", line);
    }
}

static bool can_sleep(void)
{
    uint64_t rf;
    __asm__ volatile("pushfq; pop %0" : "=r"(rf));
    if (!(rf & 0x200)) return false;
    percpu_t *pc = get_percpu();
    return pc && pc->current_task;
}

void kpi_udelay(unsigned long us)
{
    uint64_t end = sched_now_ns() + (uint64_t)us * 1000ULL;
    while (sched_now_ns() < end) __asm__ volatile("pause");
}

void kpi_msleep(unsigned long ms)
{
    if (!ms) ms = 1;
    if (can_sleep()) task_sleep_ms(ms);
    else kpi_udelay(ms * 1000UL);
}

uint64_t kpi_jiffies(void)
{
    return sched_now_ns() / 1000000ULL;
}

uint64_t kpi_ktime_ns(void)
{
    return sched_now_ns();
}

void kpi_random_bytes(void *buf, size_t len)
{
    random_bytes(buf, len);
}

struct kpi_waiter {
    task_t *task;
    struct kpi_waiter *next;
    volatile int woken;
};

void kpi_waitq_init(wait_queue_head_t *w)
{
    spin_lock_init(&w->lock);
    w->seq = 0;
    w->head = NULL;
}

void kpi_wake_up(wait_queue_head_t *w)
{
    uint64_t f = spinlock_acquire_irqsave(&w->lock);
    __atomic_add_fetch(&w->seq, 1, __ATOMIC_RELEASE);
    struct kpi_waiter *q = w->head;
    w->head = NULL;
    while (q) {
        struct kpi_waiter *n = q->next;
        task_t *t = q->task;
        __atomic_store_n(&q->woken, 1, __ATOMIC_RELEASE);
        task_unblock(t);
        q = n;
    }
    spinlock_release_irqrestore(&w->lock, f);
}

static void waiter_remove(wait_queue_head_t *w, struct kpi_waiter *me)
{
    for (struct kpi_waiter **pp = &w->head; *pp; pp = &(*pp)->next) {
        if (*pp == me) { *pp = me->next; return; }
    }
}

void kpi_wait_on(wait_queue_head_t *w, u32 seen, uint64_t timeout_ns)
{
    if (!can_sleep()) {
        uint64_t end = sched_now_ns() + (timeout_ns < 1000000ULL ? timeout_ns : 1000000ULL);
        while (__atomic_load_n(&w->seq, __ATOMIC_ACQUIRE) == seen && sched_now_ns() < end)
            __asm__ volatile("pause");
        return;
    }
    percpu_t *pc = get_percpu();
    task_t *t = (task_t *)pc->current_task;
    struct kpi_waiter me = { t, NULL, 0 };
    uint64_t f = spinlock_acquire_irqsave(&w->lock);
    if (__atomic_load_n(&w->seq, __ATOMIC_ACQUIRE) != seen) {
        spinlock_release_irqrestore(&w->lock, f);
        return;
    }
    me.next = w->head;
    w->head = &me;
    t->wakeup_time_ns = timeout_ns == ~0ULL ? 0 : sched_now_ns() + timeout_ns;
    t->runnable = false;
    t->state = TASK_BLOCKED;
    if (t->wakeup_time_ns) sched_note_wakeup(t->wakeup_time_ns);
    spinlock_release_irqrestore(&w->lock, f);
    sched_reschedule();
    f = spinlock_acquire_irqsave(&w->lock);
    if (!me.woken) waiter_remove(w, &me);
    spinlock_release_irqrestore(&w->lock, f);
    t->wakeup_time_ns = 0;
}

void kpi_init_completion(struct completion *c)
{
    c->done = 0;
    kpi_waitq_init(&c->wait);
}

void kpi_complete(struct completion *c, bool all)
{
    uint64_t f = spinlock_acquire_irqsave(&c->wait.lock);
    if (all) c->done = 0x7fffffff;
    else if (c->done != 0x7fffffff) c->done++;
    spinlock_release_irqrestore(&c->wait.lock, f);
    kpi_wake_up(&c->wait);
}

static bool completion_take(struct completion *c)
{
    uint64_t f = spinlock_acquire_irqsave(&c->wait.lock);
    bool ok = c->done != 0;
    if (ok && c->done != 0x7fffffff) c->done--;
    spinlock_release_irqrestore(&c->wait.lock, f);
    return ok;
}

unsigned long kpi_wait_for_completion_timeout(struct completion *c, unsigned long timeout)
{
    bool forever = timeout >= (~0UL >> 1);
    uint64_t end = forever ? 0 : kpi_jiffies() + timeout;
    for (;;) {
        u32 seq = __atomic_load_n(&c->wait.seq, __ATOMIC_ACQUIRE);
        if (completion_take(c)) {
            if (forever) return 1;
            long left = (long)(end - kpi_jiffies());
            return left > 0 ? (unsigned long)left : 1;
        }
        if (!forever) {
            long left = (long)(end - kpi_jiffies());
            if (left <= 0) return completion_take(c) ? 1 : 0;
            kpi_wait_on(&c->wait, seq, (uint64_t)left * 1000000ULL);
        } else {
            kpi_wait_on(&c->wait, seq, ~0ULL);
        }
    }
}

struct workqueue_struct {
    char name[24];
    spinlock_t lock;
    struct list_head pending;
    struct list_head delayed;
    wait_queue_head_t kick;
    wait_queue_head_t idle;
    struct work_struct *current_work;
    task_t *thread;
    volatile int dying;
    volatile int exited;
};

struct workqueue_struct *system_wq;

static void wq_thread(void *arg);

static struct workqueue_struct *wq_create(const char *name)
{
    struct workqueue_struct *wq = sys_zalloc(sizeof(*wq));
    if (!wq) return NULL;
    strncpy(wq->name, name ? name : "kworker", sizeof(wq->name) - 1);
    spin_lock_init(&wq->lock);
    INIT_LIST_HEAD(&wq->pending);
    INIT_LIST_HEAD(&wq->delayed);
    kpi_waitq_init(&wq->kick);
    kpi_waitq_init(&wq->idle);
    wq->thread = task_create(wq->name, wq_thread, wq, KPI_WQ_PRIO);
    if (!wq->thread) { sys_free(wq); return NULL; }
    return wq;
}

void kpi_runtime_init(void)
{
    if (!system_wq) system_wq = wq_create("kpi_events");
}

struct workqueue_struct *kpi_alloc_workqueue(const char *name)
{
    return wq_create(name);
}

void kpi_init_work(struct work_struct *w, work_func_t f)
{
    w->func = f;
    INIT_LIST_HEAD(&w->entry);
    w->pending = 0;
    w->running = 0;
    w->wq = NULL;
}

void kpi_init_delayed_work(struct delayed_work *d, work_func_t f)
{
    kpi_init_work(&d->work, f);
    INIT_LIST_HEAD(&d->tentry);
    d->armed = 0;
    d->due_ns = 0;
    d->twq = NULL;
}

static bool enqueue_locked(struct workqueue_struct *wq, struct work_struct *w)
{
    if (w->pending) return false;
    w->pending = 1;
    w->wq = wq;
    list_add_tail(&w->entry, &wq->pending);
    return true;
}

bool kpi_queue_work(struct workqueue_struct *wq, struct work_struct *w)
{
    if (!wq) wq = system_wq;
    if (!wq) return false;
    uint64_t f = spinlock_acquire_irqsave(&wq->lock);
    bool r = enqueue_locked(wq, w);
    spinlock_release_irqrestore(&wq->lock, f);
    if (r) kpi_wake_up(&wq->kick);
    return r;
}

static bool arm_locked(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay, bool modify)
{
    if (d->work.pending && !modify) return false;
    if (d->armed) {
        if (!modify) return false;
        list_del_init(&d->tentry);
        d->armed = 0;
    }
    if (d->work.pending && modify) return false;
    if (!delay) return enqueue_locked(wq, &d->work);
    d->due_ns = sched_now_ns() + (uint64_t)delay * 1000000ULL;
    d->armed = 1;
    d->twq = wq;
    list_add_tail(&d->tentry, &wq->delayed);
    return true;
}

bool kpi_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay)
{
    if (!wq) wq = system_wq;
    if (!wq) return false;
    uint64_t f = spinlock_acquire_irqsave(&wq->lock);
    bool r = arm_locked(wq, d, delay, false);
    spinlock_release_irqrestore(&wq->lock, f);
    if (r) kpi_wake_up(&wq->kick);
    return r;
}

bool kpi_mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *d, unsigned long delay)
{
    if (!wq) wq = system_wq;
    if (!wq) return false;
    struct workqueue_struct *old = d->twq;
    if (old && old != wq) kpi_cancel_delayed_work(d);
    uint64_t f = spinlock_acquire_irqsave(&wq->lock);
    bool was = d->armed || d->work.pending;
    arm_locked(wq, d, delay, true);
    spinlock_release_irqrestore(&wq->lock, f);
    kpi_wake_up(&wq->kick);
    return was;
}

static bool unqueue_work(struct work_struct *w)
{
    struct workqueue_struct *wq = w->wq;
    if (!wq) return false;
    bool removed = false;
    uint64_t f = spinlock_acquire_irqsave(&wq->lock);
    if (w->pending) {
        list_del_init(&w->entry);
        w->pending = 0;
        removed = true;
    }
    spinlock_release_irqrestore(&wq->lock, f);
    return removed;
}

static bool disarm_delayed(struct delayed_work *d)
{
    struct workqueue_struct *wq = d->twq;
    if (!wq) return false;
    bool removed = false;
    uint64_t f = spinlock_acquire_irqsave(&wq->lock);
    if (d->armed) {
        list_del_init(&d->tentry);
        d->armed = 0;
        removed = true;
    }
    spinlock_release_irqrestore(&wq->lock, f);
    return removed;
}

static bool work_is_current(struct work_struct *w)
{
    struct workqueue_struct *wq = w->wq;
    return wq && wq->current_work == w;
}

static void wait_not_running(struct work_struct *w)
{
    struct workqueue_struct *wq = w->wq;
    if (!wq) return;
    if (wq->thread && can_sleep() && get_percpu()->current_task == (void *)wq->thread) return;
    for (;;) {
        u32 seq = __atomic_load_n(&wq->idle.seq, __ATOMIC_ACQUIRE);
        if (!work_is_current(w)) return;
        kpi_wait_on(&wq->idle, seq, 10000000ULL);
    }
}

bool kpi_cancel_work_sync(struct work_struct *w)
{
    bool r = unqueue_work(w);
    wait_not_running(w);
    return r;
}

bool kpi_cancel_delayed_work(struct delayed_work *d)
{
    bool a = disarm_delayed(d);
    bool b = unqueue_work(&d->work);
    return a || b;
}

bool kpi_cancel_delayed_work_sync(struct delayed_work *d)
{
    bool r = kpi_cancel_delayed_work(d);
    wait_not_running(&d->work);
    return r;
}

bool kpi_flush_work(struct work_struct *w)
{
    struct workqueue_struct *wq = w->wq;
    if (!wq) return false;
    if (wq->thread && can_sleep() && get_percpu()->current_task == (void *)wq->thread) return false;
    bool waited = false;
    for (;;) {
        u32 seq = __atomic_load_n(&wq->idle.seq, __ATOMIC_ACQUIRE);
        if (!w->pending && !work_is_current(w)) return waited;
        waited = true;
        kpi_wait_on(&wq->idle, seq, 10000000ULL);
    }
}

bool kpi_flush_delayed_work(struct delayed_work *d)
{
    struct workqueue_struct *wq = d->twq;
    if (wq && disarm_delayed(d)) {
        uint64_t f = spinlock_acquire_irqsave(&wq->lock);
        enqueue_locked(wq, &d->work);
        spinlock_release_irqrestore(&wq->lock, f);
        kpi_wake_up(&wq->kick);
    }
    return kpi_flush_work(&d->work);
}

void kpi_flush_workqueue(struct workqueue_struct *wq)
{
    if (!wq) return;
    if (wq->thread && can_sleep() && get_percpu()->current_task == (void *)wq->thread) return;
    for (;;) {
        u32 seq = __atomic_load_n(&wq->idle.seq, __ATOMIC_ACQUIRE);
        uint64_t f = spinlock_acquire_irqsave(&wq->lock);
        bool busy = !list_empty(&wq->pending) || wq->current_work;
        spinlock_release_irqrestore(&wq->lock, f);
        if (!busy) return;
        kpi_wait_on(&wq->idle, seq, 10000000ULL);
    }
}

void kpi_destroy_workqueue(struct workqueue_struct *wq)
{
    if (!wq || wq == system_wq) return;
    kpi_flush_workqueue(wq);
    wq->dying = 1;
    kpi_wake_up(&wq->kick);
    while (!wq->exited) kpi_msleep(1);
    sys_free(wq);
}

static void wq_thread(void *arg)
{
    struct workqueue_struct *wq = arg;
    for (;;) {
        u32 seq = __atomic_load_n(&wq->kick.seq, __ATOMIC_ACQUIRE);
        uint64_t now = sched_now_ns();
        uint64_t next_due = ~0ULL;
        uint64_t f = spinlock_acquire_irqsave(&wq->lock);
        struct list_head *pos, *n;
        list_for_each_safe(pos, n, &wq->delayed) {
            struct delayed_work *d = list_entry(pos, struct delayed_work, tentry);
            if (d->due_ns <= now) {
                list_del_init(&d->tentry);
                d->armed = 0;
                enqueue_locked(wq, &d->work);
            } else if (d->due_ns < next_due) {
                next_due = d->due_ns;
            }
        }
        struct work_struct *w = NULL;
        if (!list_empty(&wq->pending)) {
            w = list_first_entry(&wq->pending, struct work_struct, entry);
            list_del_init(&w->entry);
            w->pending = 0;
            w->running = 1;
            wq->current_work = w;
        }
        spinlock_release_irqrestore(&wq->lock, f);
        if (w) {
            w->func(w);
            f = spinlock_acquire_irqsave(&wq->lock);
            w->running = 0;
            wq->current_work = NULL;
            spinlock_release_irqrestore(&wq->lock, f);
            kpi_wake_up(&wq->idle);
            continue;
        }
        kpi_wake_up(&wq->idle);
        if (wq->dying) break;
        uint64_t wait = next_due == ~0ULL ? 1000000000ULL : next_due - now;
        kpi_wait_on(&wq->kick, seq, wait);
    }
    wq->exited = 1;
    task_exit();
}

static void timer_trampoline(struct work_struct *w)
{
    struct delayed_work *d = to_delayed_work(w);
    struct timer_list *t = container_of(d, struct timer_list, dw);
    if (t->function) t->function(t);
}

void kpi_timer_setup(struct timer_list *t, void (*fn)(struct timer_list *))
{
    t->function = fn;
    kpi_init_delayed_work(&t->dw, timer_trampoline);
}

bool kpi_mod_timer(struct timer_list *t, unsigned long expires)
{
    long delay = (long)(expires - kpi_jiffies());
    if (delay < 0) delay = 0;
    return kpi_mod_delayed_work(system_wq, &t->dw, (unsigned long)delay);
}

bool kpi_timer_delete(struct timer_list *t)
{
    return kpi_cancel_delayed_work(&t->dw);
}

bool kpi_timer_delete_sync(struct timer_list *t)
{
    return kpi_cancel_delayed_work_sync(&t->dw);
}

#define SKB_SMALL     2048
#define SKB_POOL_PAGES 64

typedef struct skb_chunk { struct skb_chunk *next; } skb_chunk_t;

static spinlock_t   g_skb_lock = SPINLOCK_INIT;
static skb_chunk_t *g_skb_free;

static bool skb_pool_grow(void)
{
    uintptr_t phys;
    u8 *v = sys_dma_alloc((size_t)SKB_POOL_PAGES * PAGE_SIZE, &phys);
    if (!v) return false;
    uint64_t f = spinlock_acquire_irqsave(&g_skb_lock);
    for (size_t off = 0; off + SKB_SMALL <= (size_t)SKB_POOL_PAGES * PAGE_SIZE; off += SKB_SMALL) {
        skb_chunk_t *c = (skb_chunk_t *)(v + off);
        c->next = g_skb_free;
        g_skb_free = c;
    }
    spinlock_release_irqrestore(&g_skb_lock, f);
    return true;
}

static void *skb_data_alloc(unsigned int size, unsigned int *cap)
{
    if (size <= SKB_SMALL) {
        for (int tries = 0; tries < 2; tries++) {
            uint64_t f = spinlock_acquire_irqsave(&g_skb_lock);
            skb_chunk_t *c = g_skb_free;
            if (c) g_skb_free = c->next;
            spinlock_release_irqrestore(&g_skb_lock, f);
            if (c) { *cap = SKB_SMALL; return c; }
            if (!skb_pool_grow()) return NULL;
        }
        return NULL;
    }
    uintptr_t phys;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *p = sys_dma_alloc(pages * PAGE_SIZE, &phys);
    if (!p) return NULL;
    *cap = (unsigned int)(pages * PAGE_SIZE);
    return p;
}

static void skb_data_free(void *p, unsigned int cap)
{
    if (!p) return;
    if (cap == SKB_SMALL) {
        skb_chunk_t *c = p;
        uint64_t f = spinlock_acquire_irqsave(&g_skb_lock);
        c->next = g_skb_free;
        g_skb_free = c;
        spinlock_release_irqrestore(&g_skb_lock, f);
        return;
    }
    sys_dma_free(p, cap);
}

struct sk_buff *kpi_alloc_skb(unsigned int size, gfp_t gfp)
{
    (void)gfp;
    struct sk_buff *s = sys_zalloc(sizeof(*s));
    if (!s) return NULL;
    unsigned int cap = 0;
    s->head = skb_data_alloc(size + KPI_SKB_HEADROOM, &cap);
    if (!s->head) { sys_free(s); return NULL; }
    s->end = cap;
    s->truesize = cap;
    s->data = s->head + KPI_SKB_HEADROOM;
    s->tail = KPI_SKB_HEADROOM;
    s->len = 0;
    return s;
}

void kpi_free_skb(struct sk_buff *skb)
{
    if (!skb) return;
    skb_data_free(skb->head, skb->truesize);
    sys_free(skb);
}

struct sk_buff *kpi_skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
    unsigned int hr = skb_headroom(skb);
    struct sk_buff *n = kpi_alloc_skb(skb->len + (hr > KPI_SKB_HEADROOM ? hr - KPI_SKB_HEADROOM : 0), gfp);
    if (!n) return NULL;
    if (hr > KPI_SKB_HEADROOM) skb_reserve(n, (int)(hr - KPI_SKB_HEADROOM));
    skb_put_data(n, skb->data, skb->len);
    memcpy(n->cb, skb->cb, sizeof(n->cb));
    n->priority = skb->priority;
    n->protocol = skb->protocol;
    n->queue_mapping = skb->queue_mapping;
    n->dev = skb->dev;
    return n;
}

int kpi_dma_set_mask(struct device *dev, u64 mask)
{
    if (!dev) return -EINVAL;
    dev->dma_mask = mask;
    return 0;
}

void *kpi_dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
    (void)dev; (void)gfp;
    uintptr_t phys = 0;
    void *v = sys_dma_alloc(size, &phys);
    if (!v) return NULL;
    *handle = phys;
    return v;
}

void kpi_dma_free_coherent(struct device *dev, size_t size, void *cpu, dma_addr_t handle)
{
    (void)dev; (void)handle;
    sys_dma_free(cpu, size);
}

dma_addr_t kpi_dma_map_single(struct device *dev, void *cpu, size_t size, int dir)
{
    (void)dir;
    uintptr_t phys = pmm_virt_to_phys(cpu);
    u64 mask = dev && dev->dma_mask ? dev->dma_mask : DMA_BIT_MASK(32);
    if (!phys || phys + size - 1 > mask) return 0;
    __asm__ volatile("mfence" ::: "memory");
    return phys;
}

void kpi_dev_coredumpv(struct device *dev, void *data, size_t len)
{
    (void)dev;
    serial_printf_lvl(LOG_LEVEL_WARN, "rtw88: firmware crash dump of %zu bytes discarded\n", len);
    sys_free(data);
}

static int fw_read_file(const char *name, const struct firmware **out)
{
    static const char *const dirs[] = { "/lib/firmware/", "/usr/lib/firmware/" };
    char path[160];
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s%s", dirs[i], name);
        vfs_file_t *f = NULL;
        if (vfs_open(path, O_RDONLY, 0, &f) < 0) continue;
        vfs_stat_t st;
        if (vfs_fstat(f, &st) < 0 || st.st_size == 0 || st.st_size > (64u << 20)) {
            vfs_close(f);
            continue;
        }
        struct firmware *fw = sys_zalloc(sizeof(*fw));
        u8 *buf = sys_alloc((size_t)st.st_size);
        if (!fw || !buf) {
            sys_free(fw); sys_free(buf);
            vfs_close(f);
            return -ENOMEM;
        }
        size_t got = 0;
        while (got < st.st_size) {
            int64_t r = vfs_read(f, buf + got, (size_t)st.st_size - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        vfs_close(f);
        if (got != st.st_size) {
            sys_free(fw); sys_free(buf);
            continue;
        }
        fw->data = buf;
        fw->size = got;
        *out = fw;
        serial_printf_lvl(LOG_LEVEL_INFO, "firmware: loaded %s (%zu bytes)\n", path, got);
        return 0;
    }
    return -ENOENT;
}

int kpi_request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
    (void)dev;
    *fw = NULL;
    int r = fw_read_file(name, fw);
    if (r < 0)
        serial_printf_lvl(LOG_LEVEL_WARN, "firmware: %s not found in /lib/firmware\n", name);
    return r;
}

void kpi_release_firmware(const struct firmware *fw)
{
    if (!fw) return;
    sys_free((void *)fw->data);
    sys_free((void *)fw);
}

struct fw_async {
    struct work_struct work;
    char name[96];
    struct device *dev;
    void *ctx;
    void (*cont)(const struct firmware *fw, void *ctx);
};

static void fw_async_work(struct work_struct *w)
{
    struct fw_async *a = container_of(w, struct fw_async, work);
    const struct firmware *fw = NULL;
    if (kpi_request_firmware(&fw, a->name, a->dev) < 0) fw = NULL;
    a->cont(fw, a->ctx);
    sys_free(a);
}

int kpi_request_firmware_nowait(const char *name, struct device *dev, void *ctx,
                                void (*cont)(const struct firmware *fw, void *ctx))
{
    struct fw_async *a = sys_zalloc(sizeof(*a));
    if (!a) return -ENOMEM;
    strncpy(a->name, name, sizeof(a->name) - 1);
    a->dev = dev;
    a->ctx = ctx;
    a->cont = cont;
    kpi_init_work(&a->work, fw_async_work);
    kpi_queue_work(system_wq, &a->work);
    return 0;
}
