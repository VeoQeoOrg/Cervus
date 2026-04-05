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

extern void draw_cursor(void);
extern void erase_cursor(void);

extern uint32_t get_screen_width(void);
extern uint32_t get_screen_height(void);
extern uint32_t get_cursor_row(void);
extern uint32_t get_cursor_col(void);

#define TIOCGWINSZ   0x5413
#define TIOCGCURSOR  0x5480

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

static inline task_t* devfs_cur_task(void) {
    percpu_t* pc = get_percpu();
    return pc ? (task_t*)pc->current_task : NULL;
}

static int64_t tty_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    (void)node; (void)offset;
    if (len == 0) return 0;

    char *dst = buf;

    uint64_t freq      = hpet_is_available() ? hpet_get_frequency() : 0;
    uint64_t half_tick = freq ? freq / 2 : 0;
    uint64_t next_blink = half_tick ? (hpet_read_counter() + half_tick) : 0;
    int      cursor_on  = 1;
    draw_cursor();

    while (kb_buf_empty()) {
        task_t *me = devfs_cur_task();
        if (me && me->pending_kill) {
            erase_cursor();
            return -EINTR;
        }

        if (half_tick) {
            uint64_t now = hpet_read_counter();
            if (now >= next_blink) {
                if (cursor_on) { erase_cursor(); cursor_on = 0; }
                else           { draw_cursor();  cursor_on = 1; }
                next_blink = now + half_tick;
            }
        }
        task_yield();
    }

    erase_cursor();

    task_t *me = devfs_cur_task();
    if (me && me->pending_kill) {
        kb_buf_consume_ctrlc();
        return -EINTR;
    }

    char first = kb_buf_getc();
    if (first == 0x03) {
        return -EINTR;
    }

    dst[0] = first;
    size_t got = 1;

    while (got < len) {
        char c;
        if (!kb_buf_try_getc(&c)) break;
        if (c == 0x03) break;
        dst[got++] = c;
        if (c == '\n') break;
    }
    return (int64_t)got;
}

static int64_t tty_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    (void)node; (void)offset;
    serial_writebuf((const char *)buf, len);
    printf("%.*s", (int)len, (const char *)buf);
    return (int64_t)len;
}

static int64_t tty_ioctl(vnode_t *node, uint64_t req, void *arg) {
    (void)node;

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
        cp->row = get_cursor_row();
        cp->col = get_cursor_col();
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

#define DEVFS_MAX_ENTRIES 16

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

static void devfs_register(const char *name, vnode_t *node) {
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