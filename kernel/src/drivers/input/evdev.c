#include "../../../include/fs/vfs.h"
#include "../../../include/fs/devfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/sched.h"
#include "../../../include/drivers/timer.h"
#include "../../../include/syscall/errno.h"
#include <string.h>

#define EV_RING    512
#define EV_WAITERS 8

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_MSC 0x04
#define EV_LED 0x11
#define EV_REP 0x14
#define EV_MAX 0x1f

#define SYN_REPORT  0
#define SYN_DROPPED 3

#define REL_X      0x00
#define REL_Y      0x01
#define REL_HWHEEL 0x06
#define REL_WHEEL  0x08
#define REL_WHEEL_HI_RES 0x0b
#define REL_MAX    0x0f

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

#define KEY_MAX    0x2ff
#define KEY_BYTES  ((KEY_MAX + 8) / 8)

#define LED_NUML    0
#define LED_CAPSL   1
#define LED_SCROLLL 2

#define BUS_USB    0x03
#define BUS_I8042  0x11

typedef struct {
    uint64_t sec;
    uint64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} __attribute__((packed)) input_event_t;

_Static_assert(sizeof(input_event_t) == 24, "input_event layout");

typedef struct {
    input_event_t ring[EV_RING];
    uint64_t      seq;
    spinlock_t    lock;
    task_t       *waiters[EV_WAITERS];
    const char   *name;
    uint16_t      bustype;
    uint16_t      product;
    uint8_t       key_state[KEY_BYTES];
    int           is_mouse;
} evdev_t;

static evdev_t g_kbd       = { .name = "input0" };
static evdev_t g_mouse     = { .name = "input1" };
static evdev_t g_ev_kbd    = { .name = "Cervus keyboard", .bustype = BUS_I8042, .product = 1 };
static evdev_t g_ev_mouse  = { .name = "Cervus mouse",    .bustype = BUS_USB,   .product = 2,
                               .is_mouse = 1 };

static void ev_stamp(input_event_t *e, uint16_t type, uint16_t code, int32_t value)
{
    uint64_t now = sched_now_ns();
    e->sec   = now / 1000000000ULL;
    e->usec  = (now % 1000000000ULL) / 1000ULL;
    e->type  = type;
    e->code  = code;
    e->value = value;
}

static void ev_push(evdev_t *d, uint16_t type, uint16_t code, int32_t value)
{
    task_t *wake[EV_WAITERS];
    uint64_t f = spinlock_acquire_irqsave(&d->lock);
    ev_stamp(&d->ring[d->seq % EV_RING], type, code, value);
    d->seq++;
    if (type == EV_KEY && code <= KEY_MAX) {
        if (value) d->key_state[code / 8] |=  (uint8_t)(1u << (code % 8));
        else       d->key_state[code / 8] &= (uint8_t)~(1u << (code % 8));
    }
    for (int i = 0; i < EV_WAITERS; i++) { wake[i] = d->waiters[i]; d->waiters[i] = NULL; }
    spinlock_release_irqrestore(&d->lock, f);
    for (int i = 0; i < EV_WAITERS; i++) if (wake[i]) task_unblock(wake[i]);
}

void input_report_key(int keycode, int pressed)
{
    ev_push(&g_kbd, EV_KEY, (uint16_t)keycode, pressed ? 1 : 0);
    ev_push(&g_kbd, EV_SYN, SYN_REPORT, 0);
}

void input_report_linux_key(int keycode, int pressed)
{
    if (keycode <= 0 || keycode > KEY_MAX) return;
    ev_push(&g_ev_kbd, EV_KEY, (uint16_t)keycode, pressed ? 1 : 0);
    ev_push(&g_ev_kbd, EV_SYN, SYN_REPORT, 0);
}

static const uint8_t SET1_E0_TO_LINUX[128] = {
    [0x1C] = 96,  [0x1D] = 97,  [0x35] = 98,  [0x37] = 99,
    [0x38] = 100, [0x47] = 102, [0x48] = 103, [0x49] = 104,
    [0x4B] = 105, [0x4D] = 106, [0x4F] = 107, [0x50] = 108,
    [0x51] = 109, [0x52] = 110, [0x53] = 111, [0x5B] = 125,
    [0x5C] = 126, [0x5D] = 127,
};

int input_set1_to_linux(int scancode, int extended)
{
    scancode &= 0x7F;
    if (extended) return SET1_E0_TO_LINUX[scancode];
    if (scancode >= 0x01 && scancode <= 0x58) return scancode;
    return 0;
}

void input_report_motion(int dx, int dy, int buttons, int prev_buttons)
{
    if (dx) ev_push(&g_mouse, EV_REL, REL_X, dx);
    if (dy) ev_push(&g_mouse, EV_REL, REL_Y, dy);
    for (int b = 0; b < 3; b++) {
        int now = (buttons >> b) & 1, was = (prev_buttons >> b) & 1;
        if (now != was) ev_push(&g_mouse, EV_KEY, (uint16_t)(BTN_LEFT + b), now);
    }
    ev_push(&g_mouse, EV_SYN, SYN_REPORT, 0);

    if (dx) ev_push(&g_ev_mouse, EV_REL, REL_X, dx);
    if (dy) ev_push(&g_ev_mouse, EV_REL, REL_Y, dy);
    for (int b = 0; b < 3; b++) {
        int now = (buttons >> b) & 1, was = (prev_buttons >> b) & 1;
        if (now != was) ev_push(&g_ev_mouse, EV_KEY, (uint16_t)(BTN_LEFT + b), now);
    }
    ev_push(&g_ev_mouse, EV_SYN, SYN_REPORT, 0);
}

void input_report_wheel(int dz)
{
    if (!dz) return;
    ev_push(&g_ev_mouse, EV_REL, REL_WHEEL, dz);
    ev_push(&g_ev_mouse, EV_REL, REL_WHEEL_HI_RES, dz * 120);
    ev_push(&g_ev_mouse, EV_SYN, SYN_REPORT, 0);
}

static void ev_open_file(vnode_t *n, vfs_file_t *file)
{
    evdev_t *d = (evdev_t *)n->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&d->lock);
    file->offset = d->seq;
    spinlock_release_irqrestore(&d->lock, f);
}

static int ev_wait_slot(evdev_t *d, task_t *me)
{
    for (int i = 0; i < EV_WAITERS; i++)
        if (d->waiters[i] == me) return 1;
    for (int i = 0; i < EV_WAITERS; i++)
        if (!d->waiters[i]) { d->waiters[i] = me; return 1; }
    return 0;
}

static int64_t ev_read_file(vfs_file_t *file, void *buf, size_t len)
{
    evdev_t *d = (evdev_t *)file->vnode->fs_data;
    size_t want = len / sizeof(input_event_t);
    if (!want) return -EINVAL;

    extern task_t *syscall_cur_task(void);
    task_t *me = syscall_cur_task();
    int nonblock = vfs_io_nonblock();
    input_event_t *dst = (input_event_t *)buf;

    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&d->lock);
        uint64_t pos = file->offset;
        if (pos > d->seq) pos = d->seq;
        size_t got = 0;
        if (d->seq - pos > EV_RING) {
            ev_stamp(&dst[got++], EV_SYN, SYN_DROPPED, 0);
            pos = d->seq;
        }
        while (got < want && pos < d->seq) {
            dst[got++] = d->ring[pos % EV_RING];
            pos++;
        }
        file->offset = pos;
        if (got) {
            spinlock_release_irqrestore(&d->lock, f);
            return (int64_t)(got * sizeof(input_event_t));
        }
        if (nonblock) {
            spinlock_release_irqrestore(&d->lock, f);
            return -EAGAIN;
        }
        if (me && me->pending_kill) {
            spinlock_release_irqrestore(&d->lock, f);
            return -EINTR;
        }
        int blocked = me && ev_wait_slot(d, me);
        if (blocked) {
            me->runnable = false;
            me->state    = TASK_BLOCKED;
        }
        spinlock_release_irqrestore(&d->lock, f);
        if (!blocked) { task_yield(); continue; }
        sched_reschedule();
        f = spinlock_acquire_irqsave(&d->lock);
        for (int i = 0; i < EV_WAITERS; i++)
            if (d->waiters[i] == me) d->waiters[i] = NULL;
        spinlock_release_irqrestore(&d->lock, f);
    }
}

static int ev_poll_file(vfs_file_t *file, int events)
{
    evdev_t *d = (evdev_t *)file->vnode->fs_data;
    int out = 0;
    uint64_t f = spinlock_acquire_irqsave(&d->lock);
    if ((events & POLLIN) && file->offset < d->seq) out |= POLLIN;
    spinlock_release_irqrestore(&d->lock, f);
    return out;
}

#define IOC_NR(c)   ((uint32_t)(c) & 0xffu)
#define IOC_TYPE(c) (((uint32_t)(c) >> 8) & 0xffu)
#define IOC_SIZE(c) (((uint32_t)(c) >> 16) & 0x3fffu)

static void set_bit(uint8_t *bits, unsigned n) { bits[n / 8] |= (uint8_t)(1u << (n % 8)); }

static size_t put_bits(void *arg, size_t cap, const uint8_t *bits, size_t have)
{
    size_t n = have < cap ? have : cap;
    memset(arg, 0, cap);
    memcpy(arg, bits, n);
    return n;
}

static void fill_ev_bits(evdev_t *d, uint8_t *bits)
{
    memset(bits, 0, 4);
    set_bit(bits, EV_SYN);
    set_bit(bits, EV_KEY);
    if (d->is_mouse) set_bit(bits, EV_REL);
    else             set_bit(bits, EV_REP);
}

static void fill_key_bits(evdev_t *d, uint8_t *bits)
{
    memset(bits, 0, KEY_BYTES);
    if (d->is_mouse) {
        set_bit(bits, BTN_LEFT);
        set_bit(bits, BTN_RIGHT);
        set_bit(bits, BTN_MIDDLE);
        return;
    }
    for (unsigned k = 1; k <= 88; k++) set_bit(bits, k);
    for (unsigned k = 96; k <= 111; k++) set_bit(bits, k);
    for (unsigned k = 125; k <= 127; k++) set_bit(bits, k);
}

static void fill_rel_bits(evdev_t *d, uint8_t *bits)
{
    memset(bits, 0, 2);
    if (!d->is_mouse) return;
    set_bit(bits, REL_X);
    set_bit(bits, REL_Y);
    set_bit(bits, REL_WHEEL);
    set_bit(bits, REL_WHEEL_HI_RES);
}

static int64_t ev_ioctl(vnode_t *n, uint64_t req, void *arg)
{
    evdev_t *d = (evdev_t *)n->fs_data;
    if (IOC_TYPE(req) != 'E') return -ENOTTY;

    uint32_t nr   = IOC_NR(req);
    size_t   size = IOC_SIZE(req);

    switch (nr) {
    case 0x01:
        if (!arg) return -EFAULT;
        *(int32_t *)arg = 0x010001;
        return 0;
    case 0x02: {
        if (!arg) return -EFAULT;
        uint16_t *id = arg;
        id[0] = d->bustype;
        id[1] = 0x1D6B;
        id[2] = d->product;
        id[3] = 1;
        return 0;
    }
    case 0x03:
        if (!arg) return -EFAULT;
        ((uint32_t *)arg)[0] = 250;
        ((uint32_t *)arg)[1] = 33;
        return 0;
    case 0x06: {
        if (!arg || !size) return -EFAULT;
        size_t l = strlen(d->name);
        if (l >= size) l = size - 1;
        memset(arg, 0, size);
        memcpy(arg, d->name, l);
        return (int64_t)(l + 1);
    }
    case 0x07:
    case 0x08: {
        if (!arg || !size) return -EFAULT;
        memset(arg, 0, size);
        return -ENOENT;
    }
    case 0x09:
        if (!arg) return -EFAULT;
        memset(arg, 0, size);
        return (int64_t)size;
    case 0x18: {
        if (!arg) return -EFAULT;
        uint64_t f = spinlock_acquire_irqsave(&d->lock);
        size_t r = put_bits(arg, size, d->key_state, KEY_BYTES);
        spinlock_release_irqrestore(&d->lock, f);
        return (int64_t)r;
    }
    case 0x19:
    case 0x1a:
    case 0x1b:
        if (!arg) return -EFAULT;
        memset(arg, 0, size);
        return (int64_t)size;
    case 0x90:
    case 0x91:
    case 0xa0:
        return 0;
    default:
        break;
    }

    if (nr >= 0x20 && nr < 0x20 + EV_MAX + 1) {
        if (!arg) return -EFAULT;
        uint8_t bits[KEY_BYTES];
        switch (nr - 0x20) {
        case 0:      fill_ev_bits(d, bits);  return (int64_t)put_bits(arg, size, bits, 4);
        case EV_KEY: fill_key_bits(d, bits); return (int64_t)put_bits(arg, size, bits, KEY_BYTES);
        case EV_REL: fill_rel_bits(d, bits); return (int64_t)put_bits(arg, size, bits, 2);
        default:     memset(arg, 0, size);   return (int64_t)size;
        }
    }

    if (nr >= 0x40 && nr < 0x80) {
        if (!arg) return -EFAULT;
        memset(arg, 0, size);
        return -EINVAL;
    }

    return -EINVAL;
}

static int ev_stat(vnode_t *n, vfs_stat_t *out)
{
    memset(out, 0, sizeof *out);
    out->st_ino   = n->ino;
    out->st_type  = VFS_NODE_CHARDEV;
    out->st_mode  = 0020000 | (n->mode & 0777);
    out->st_nlink = 1;
    return 0;
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
    .open_file = ev_open_file,
    .read_file = ev_read_file,
    .poll_file = ev_poll_file,
    .ioctl = ev_ioctl,
    .stat  = ev_stat,
    .ref   = ev_ref,
    .unref = ev_unref,
};

static vnode_t g_kbd_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0600, .ops = &EV_OPS,
    .fs_data = &g_kbd, .refcount = 1, .ino = 800,
};
static vnode_t g_mouse_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0600, .ops = &EV_OPS,
    .fs_data = &g_mouse, .refcount = 1, .ino = 801,
};
static vnode_t g_ev_kbd_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0660, .ops = &EV_OPS,
    .fs_data = &g_ev_kbd, .refcount = 1, .ino = 13 * 256 + 64, .rdev = 13 * 256 + 64,
};
static vnode_t g_ev_mouse_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0660, .ops = &EV_OPS,
    .fs_data = &g_ev_mouse, .refcount = 1, .ino = 13 * 256 + 65, .rdev = 13 * 256 + 65,
};

void evdev_init(void)
{
    g_kbd.lock      = (spinlock_t)SPINLOCK_INIT;
    g_mouse.lock    = (spinlock_t)SPINLOCK_INIT;
    g_ev_kbd.lock   = (spinlock_t)SPINLOCK_INIT;
    g_ev_mouse.lock = (spinlock_t)SPINLOCK_INIT;
    devfs_register("input0", &g_kbd_node);
    devfs_register("input1", &g_mouse_node);
    devfs_register_in("input", "event0", &g_ev_kbd_node);
    devfs_register_in("input", "event1", &g_ev_mouse_node);
}
