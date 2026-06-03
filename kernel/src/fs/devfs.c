#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../../include/fs/devfs.h"
#include "../../include/fs/vfs.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include "../../include/drivers/ps2.h"
#include "../../include/sched/sched.h"
#include "../../include/apic/apic.h"
#include "../../include/smp/percpu.h"
#include "../../include/console/console.h"

extern void draw_cursor(void);
extern void erase_cursor(void);
extern int  console_cursor_visible(void);
extern int  putchar(int);
extern void putchar_flush_begin(void);
extern void putchar_flush_end(void);

extern uint32_t get_screen_width(void);
extern uint32_t get_screen_height(void);
extern uint32_t get_cursor_row(void);
extern uint32_t get_cursor_col(void);

extern void vt_write(int vt, const char *buf, size_t len);
extern int  vt_active(void);
extern void vt_get_cursor(int vt, uint32_t *row, uint32_t *col);

#define TIOCGWINSZ    0x5413
#define TIOCGCURSOR   0x5480
#define TCGETS        0x5401
#define TCSETS        0x5402
#define TCSETSW       0x5403
#define TCSETSF       0x5404
#define TIOCSNONBLOCK 0x5481

#define T_ICANON  0x0002
#define T_ECHO    0x0008
#define T_ISIG    0x0001

struct cervus_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

struct cervus_cursor_pos {
    uint32_t row;
    uint32_t col;
};

struct cervus_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[32];
};

#define TTY_LINE_MAX 4096

typedef struct {
    char     ring[KB_BUF_SIZE];
    volatile uint16_t r_head, r_tail;
    struct cervus_termios termios;
    char     line[TTY_LINE_MAX];
    size_t   line_len, line_read;
    int      line_eof;
    int      nonblock;
} vt_tty_t;

static vt_tty_t g_vtty[VT_COUNT];

static void vtty_reset(vt_tty_t *t) {
    t->r_head = t->r_tail = 0;
    t->termios.c_iflag = 0;
    t->termios.c_oflag = 0;
    t->termios.c_cflag = 0;
    t->termios.c_lflag = T_ICANON | T_ECHO | T_ISIG;
    memset(t->termios.c_cc, 0, sizeof(t->termios.c_cc));
    t->line_len = 0;
    t->line_read = 0;
    t->line_eof = 0;
    t->nonblock = 0;
}

void tty_vt_init(void) {
    for (int i = 0; i < VT_COUNT; i++) vtty_reset(&g_vtty[i]);
}

void tty_vt_input(int vt, char c) {
    if (vt < 0 || vt >= VT_COUNT) return;
    vt_tty_t *t = &g_vtty[vt];
    uint16_t next = (uint16_t)((t->r_tail + 1) % KB_BUF_SIZE);
    if (next != t->r_head) {
        t->ring[t->r_tail] = c;
        t->r_tail = next;
    }
}

static int ring_empty(vt_tty_t *t) { return t->r_head == t->r_tail; }

static int ring_getc(vt_tty_t *t, char *out) {
    if (ring_empty(t)) return 0;
    *out = t->ring[t->r_head];
    t->r_head = (uint16_t)((t->r_head + 1) % KB_BUF_SIZE);
    return 1;
}

static inline task_t* devfs_cur_task(void) {
    percpu_t* pc = get_percpu();
    return pc ? (task_t*)pc->current_task : NULL;
}

static int cur_vt(void) {
    task_t *t = devfs_cur_task();
    int v = t ? t->ctty : 0;
    if (v < 0 || v >= VT_COUNT) v = 0;
    return v;
}

bool tty_has_isig_global(void) {
    return (g_vtty[vt_active()].termios.c_lflag & T_ISIG) != 0;
}

void tty_reset_nonblock(void) {
    g_vtty[cur_vt()].nonblock = 0;
}

extern void console_reset_attrs(void);

void tty_reset_on_exit(void) {
    int vt = cur_vt();
    g_vtty[vt].nonblock = 0;
    if (vt == vt_active()) console_reset_attrs();
}

static void tty_echo_char(int vt, char c) {
    if (c == '\b' || c == 0x7F) {
        vt_write(vt, "\b \b", 3);
        return;
    }
    if (c == '\n' || c == '\r') {
        vt_write(vt, "\n", 1);
        return;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c == 0x7F) return;
    char b[1] = { c };
    vt_write(vt, b, 1);
}

static int wait_for_char(int vt, vt_tty_t *t, char *out,
                         uint64_t half_tick, uint64_t *next_blink, int *cursor_on)
{
    while (ring_empty(t) || vt != vt_active()) {
        task_t *me = devfs_cur_task();
        if (me && me->pending_kill) {
            if (*cursor_on) { vt_cursor(vt, 0); *cursor_on = 0; }
            return -1;
        }
        if (vt == vt_active() && half_tick) {
            uint64_t now = hpet_read_counter();
            if (now >= *next_blink) {
                if (*cursor_on) { vt_cursor(vt, 0); *cursor_on = 0; }
                else            { vt_cursor(vt, 1); *cursor_on = 1; }
                *next_blink = now + half_tick;
            }
        } else if (vt != vt_active() && *cursor_on) {
            *cursor_on = 0;
        }
        if (me && hpet_is_available()) {
            me->wakeup_time_ns = hpet_elapsed_ns() + 8000000ULL;
            sched_note_wakeup(me->wakeup_time_ns);
            me->runnable = false;
            me->state = TASK_BLOCKED;
            sched_reschedule();
        } else {
            task_yield();
        }
    }
    if (*cursor_on) { vt_cursor(vt, 0); *cursor_on = 0; }
    ring_getc(t, out);
    return 0;
}

static int64_t tty_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    (void)node; (void)offset;
    if (len == 0) return 0;

    int       vt        = cur_vt();
    vt_tty_t *t         = &g_vtty[vt];
    char     *dst       = buf;

    int      want_cursor = console_cursor_visible();
    uint64_t freq        = hpet_is_available() ? hpet_get_frequency() : 0;
    uint64_t half_tick   = (want_cursor && freq) ? freq / 2 : 0;
    uint64_t next_blink  = half_tick ? (hpet_read_counter() + half_tick) : 0;
    int      cursor_on   = want_cursor ? 1 : 0;
    int      canonical   = (t->termios.c_lflag & T_ICANON) != 0;
    int      isig        = (t->termios.c_lflag & T_ISIG) != 0;
    int      echo        = canonical && (t->termios.c_lflag & T_ECHO) != 0;

    if (t->nonblock && ring_empty(t)
        && (!canonical || t->line_read >= t->line_len))
        return -EAGAIN;

    if (want_cursor) vt_cursor(vt, 1);

    if (!canonical) {
        char c;
        if (wait_for_char(vt, t, &c, half_tick, &next_blink, &cursor_on) < 0)
            return -EINTR;
        if (isig && c == 0x03) return -EINTR;
        dst[0] = c;
        size_t got = 1;
        while (got < len) {
            char nc;
            if (!ring_getc(t, &nc)) break;
            dst[got++] = nc;
        }
        return (int64_t)got;
    }

    if (t->line_read >= t->line_len) {
        t->line_len  = 0;
        t->line_read = 0;
        t->line_eof  = 0;

        for (;;) {
            char c;
            if (wait_for_char(vt, t, &c, half_tick, &next_blink, &cursor_on) < 0)
                return -EINTR;

            if (isig && c == 0x03) {
                t->line_len  = 0;
                t->line_read = 0;
                return -EINTR;
            }

            if (c == 0x04) {
                t->line_eof = 1;
                break;
            }

            if (c == 0x1B) {
                char nc;
                if (!ring_getc(t, &nc)) continue;
                if (nc == '[' || nc == 'O') {
                    for (;;) {
                        char fc;
                        if (!ring_getc(t, &fc)) break;
                        if ((unsigned char)fc >= 0x40 && (unsigned char)fc <= 0x7E) break;
                    }
                }
                continue;
            }

            if (c == '\b' || c == 0x7F) {
                if (t->line_len > 0) {
                    t->line_len--;
                    if (echo) tty_echo_char(vt, '\b');
                }
                continue;
            }

            if (c == '\r') c = '\n';

            if (t->line_len < TTY_LINE_MAX) {
                t->line[t->line_len++] = c;
                if (echo) tty_echo_char(vt, c);
            }

            if (c == '\n') break;
        }
    }

    if (t->line_len == 0 && t->line_eof) {
        t->line_eof = 0;
        return 0;
    }

    size_t avail   = t->line_len - t->line_read;
    size_t deliver = (avail < len) ? avail : len;
    if (deliver > 0) {
        memcpy(dst, t->line + t->line_read, deliver);
        t->line_read += deliver;
    }
    return (int64_t)deliver;
}

static int64_t tty_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    (void)node; (void)offset;
    task_t *me = devfs_cur_task();
    if (me && me->pending_kill) return (int64_t)len;
    vt_write(cur_vt(), (const char *)buf, len);
    return (int64_t)len;
}

static int64_t tty_ioctl(vnode_t *node, uint64_t req, void *arg) {
    (void)node;
    vt_tty_t *t = &g_vtty[cur_vt()];

    if (req == TIOCGWINSZ) {
        if (!arg) return -EFAULT;
        struct cervus_winsize *ws = (struct cervus_winsize *)arg;
        uint32_t w = get_screen_width();
        uint32_t h = get_screen_height();
        ws->ws_col    = (uint16_t)(w / 8);
        ws->ws_row    = (uint16_t)(h / 16);
        ws->ws_xpixel = (uint16_t)w;
        ws->ws_ypixel = (uint16_t)h;
        return 0;
    }

    if (req == TIOCGCURSOR) {
        if (!arg) return -EFAULT;
        struct cervus_cursor_pos *cp = (struct cervus_cursor_pos *)arg;
        uint32_t r = 0, c = 0;
        vt_get_cursor(cur_vt(), &r, &c);
        cp->row = r;
        cp->col = c;
        return 0;
    }

    if (req == TIOCSNONBLOCK) {
        if (!arg) return -EFAULT;
        t->nonblock = *((int *)arg) ? 1 : 0;
        return 0;
    }

    if (req == TCGETS) {
        if (!arg) return -EFAULT;
        memcpy(arg, &t->termios, sizeof(t->termios));
        return 0;
    }

    if (req == TCSETS || req == TCSETSW || req == TCSETSF) {
        if (!arg) return -EFAULT;
        memcpy(&t->termios, arg, sizeof(t->termios));
        return 0;
    }

    return -ENOTTY;
}

static void devfs_ref(vnode_t *node) {
    (void)node;
}

static void devfs_unref(vnode_t *node) {
    (void)node;
}

static int devfs_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = node->type;
    out->st_mode = node->mode;
    return 0;
}

static const vnode_ops_t tty_ops = {
    .read   = tty_read,
    .write  = tty_write,
    .ioctl  = tty_ioctl,
    .stat   = devfs_stat,
    .ref    = devfs_ref,
    .unref  = devfs_unref,
};

static int64_t null_read(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)n; (void)buf; (void)len; (void)off;
    return 0;
}

static int64_t null_write(vnode_t *n, const void *buf, size_t len, uint64_t off) {
    (void)n; (void)buf; (void)off;
    return (int64_t)len;
}

static const vnode_ops_t null_ops = {
    .read   = null_read,
    .write  = null_write,
    .stat   = devfs_stat,
    .ref    = devfs_ref,
    .unref  = devfs_unref,
};

static int64_t zero_read(vnode_t *n, void *buf, size_t len, uint64_t off) {
    (void)n; (void)off;
    memset(buf, 0, len);
    return (int64_t)len;
}

static const vnode_ops_t zero_ops = {
    .read   = zero_read,
    .write  = null_write,
    .stat   = devfs_stat,
    .ref    = devfs_ref,
    .unref  = devfs_unref,
};

#define DEVFS_MAX_ENTRIES 32

typedef struct {
    char      name[VFS_MAX_NAME];
    vnode_t  *node;
} devfs_entry_t;

typedef struct {
    devfs_entry_t entries[DEVFS_MAX_ENTRIES];
    int           count;
} devfs_dir_data_t;

static devfs_dir_data_t g_devdir;
static vnode_t          g_devroot;
static vnode_t          g_tty_node;
static vnode_t          g_null_node;
static vnode_t          g_zero_node;

static uint64_t g_devfs_ino = 100;

static int devfs_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    (void)dir;
    for (int i = 0; i < g_devdir.count; i++) {
        if (strcmp(g_devdir.entries[i].name, name) == 0) {
            vnode_ref(g_devdir.entries[i].node);
            *out = g_devdir.entries[i].node;
            return 0;
        }
    }
    return -ENOENT;
}

static int devfs_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if ((int64_t)index >= g_devdir.count) return -ENOENT;
    devfs_entry_t *e = &g_devdir.entries[index];
    out->d_ino  = e->node->ino;
    out->d_type = (uint8_t)e->node->type;
    strncpy(out->d_name, e->name, VFS_MAX_NAME - 1);
    out->d_name[VFS_MAX_NAME - 1] = '\0';
    return 0;
}

static const vnode_ops_t devfs_dir_ops = {
    .lookup  = devfs_dir_lookup,
    .readdir = devfs_dir_readdir,
    .stat    = devfs_stat,
    .ref     = devfs_ref,
    .unref   = devfs_unref,
};

void devfs_register(const char *name, vnode_t *node) {
    for (int i = 0; i < g_devdir.count; i++) {
        if (strcmp(g_devdir.entries[i].name, name) == 0) {
            g_devdir.entries[i].node = node;
            return;
        }
    }
    if (g_devdir.count >= DEVFS_MAX_ENTRIES) return;
    devfs_entry_t *e = &g_devdir.entries[g_devdir.count++];
    strncpy(e->name, name, VFS_MAX_NAME - 1);
    e->name[VFS_MAX_NAME - 1] = '\0';
    e->node = node;
}

vnode_t *devfs_create_root(void) {
    memset(&g_devdir,    0, sizeof(g_devdir));
    memset(&g_devroot,   0, sizeof(g_devroot));
    memset(&g_tty_node,  0, sizeof(g_tty_node));
    memset(&g_null_node, 0, sizeof(g_null_node));
    memset(&g_zero_node, 0, sizeof(g_zero_node));

    tty_vt_init();

    g_devroot.type     = VFS_NODE_DIR;
    g_devroot.mode     = 0755;
    g_devroot.ino      = g_devfs_ino++;
    g_devroot.ops      = &devfs_dir_ops;
    g_devroot.fs_data  = &g_devdir;
    g_devroot.refcount = 1;

    g_tty_node.type     = VFS_NODE_CHARDEV;
    g_tty_node.mode     = 0666;
    g_tty_node.ino      = g_devfs_ino++;
    g_tty_node.ops      = &tty_ops;
    g_tty_node.refcount = 1;

    g_null_node.type     = VFS_NODE_CHARDEV;
    g_null_node.mode     = 0666;
    g_null_node.ino      = g_devfs_ino++;
    g_null_node.ops      = &null_ops;
    g_null_node.refcount = 1;

    g_zero_node.type     = VFS_NODE_CHARDEV;
    g_zero_node.mode     = 0666;
    g_zero_node.ino      = g_devfs_ino++;
    g_zero_node.ops      = &zero_ops;
    g_zero_node.refcount = 1;

    devfs_register("tty",  &g_tty_node);
    devfs_register("null", &g_null_node);
    devfs_register("zero", &g_zero_node);

    serial_writestring("[devfs] /dev/tty, /dev/null, /dev/zero registered\n");
    return &g_devroot;
}
