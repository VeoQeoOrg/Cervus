#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/syscall/errno.h"
#include <string.h>

#define EV_RING 256

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02

#define SYN_REPORT 0

typedef struct {
    uint64_t sec;
    uint64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} __attribute__((packed)) input_event_t;

typedef struct {
    input_event_t ring[EV_RING];
    uint32_t      head, tail;
    spinlock_t    lock;
    task_t       *reader;
    const char   *name;
} evdev_t;

static evdev_t g_kbd  = { .name = "event0" };
static evdev_t g_mouse = { .name = "event1" };

static uint32_t ev_count(evdev_t *d) { return (d->tail - d->head + EV_RING) % EV_RING; }

static void ev_push(evdev_t *d, uint16_t type, uint16_t code, int32_t value)
{
    uint64_t f = spinlock_acquire_irqsave(&d->lock);
    uint32_t next = (d->tail + 1) % EV_RING;
    if (next == d->head) d->head = (d->head + 1) % EV_RING;
    {
        uint64_t now = sched_now_ns();
        d->ring[d->tail].sec   = now / 1000000000ULL;
        d->ring[d->tail].usec  = (now % 1000000000ULL) / 1000ULL;
        d->ring[d->tail].type  = type;
        d->ring[d->tail].code  = code;
        d->ring[d->tail].value = value;
        d->tail = next;
    }
    task_t *r = d->reader;
    d->reader = NULL;
    spinlock_release_irqrestore(&d->lock, f);
    if (r) task_unblock(r);
}

void input_report_key(int keycode, int pressed)
{
    ev_push(&g_kbd, EV_KEY, (uint16_t)keycode, pressed ? 1 : 0);
    ev_push(&g_kbd, EV_SYN, SYN_REPORT, 0);
}

void input_report_motion(int dx, int dy, int buttons, int prev_buttons)
{
    if (dx) ev_push(&g_mouse, EV_REL, 0, dx);
    if (dy) ev_push(&g_mouse, EV_REL, 1, dy);
    for (int b = 0; b < 3; b++) {
        int now = (buttons >> b) & 1, was = (prev_buttons >> b) & 1;
        if (now != was) ev_push(&g_mouse, EV_KEY, (uint16_t)(0x110 + b), now);
    }
    ev_push(&g_mouse, EV_SYN, SYN_REPORT, 0);
}

static int64_t ev_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)off;
    evdev_t *d = (evdev_t *)n->fs_data;
    if (len < sizeof(input_event_t)) return -EINVAL;

    task_t *me = NULL;
    extern task_t *syscall_cur_task(void);
    me = syscall_cur_task();

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&d->lock);
        if (ev_count(d)) {
            size_t want = len / sizeof(input_event_t);
            size_t got = 0;
            uint8_t *dst = (uint8_t *)buf;
            while (got < want && ev_count(d)) {
                memcpy(dst + got * sizeof(input_event_t), &d->ring[d->head],
                       sizeof(input_event_t));
                d->head = (d->head + 1) % EV_RING;
                got++;
            }
            spinlock_release_irqrestore(&d->lock, f);
            return (int64_t)(got * sizeof(input_event_t));
        }
        d->reader = me;
        spinlock_release_irqrestore(&d->lock, f);

        if (me) {
            me->runnable = false;
            me->state = TASK_BLOCKED;
            sched_reschedule();
            if (me->pending_kill) return -EINTR;
        } else {
            task_yield();
        }
    }
}

static int ev_poll(vnode_t *n, int events)
{
    evdev_t *d = (evdev_t *)n->fs_data;
    int out = 0;
    uint64_t f = spinlock_acquire_irqsave(&d->lock);
    if ((events & POLLIN) && ev_count(d)) out |= POLLIN;
    spinlock_release_irqrestore(&d->lock, f);
    return out;
}

static void ev_ref(vnode_t *n)
{
    n->refcount++;
}

static void ev_unref(vnode_t *n)
{
    if (n->refcount > 0) n->refcount--;
}

static const vnode_ops_t EV_OPS = {
    .read  = ev_read,
    .poll  = ev_poll,
    .ref   = ev_ref,
    .unref = ev_unref,
};

static vnode_t g_kbd_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0600, .ops = &EV_OPS,
    .fs_data = &g_kbd, .refcount = 1,
};
static vnode_t g_mouse_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0600, .ops = &EV_OPS,
    .fs_data = &g_mouse, .refcount = 1,
};

void evdev_init(void)
{
    extern void devfs_register(const char *name, vnode_t *node);
    g_kbd.lock   = (spinlock_t)SPINLOCK_INIT;
    g_mouse.lock = (spinlock_t)SPINLOCK_INIT;
    devfs_register("input0", &g_kbd_node);
    devfs_register("input1", &g_mouse_node);
}
