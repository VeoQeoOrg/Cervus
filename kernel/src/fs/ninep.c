#include "../../include/fs/vfs.h"
#include "../../include/net/tcp.h"
#include "../../include/net/net.h"
#include "../../include/sched/spinlock.h"
#include "../../include/sched/sched.h"
#include "../../include/memory/pmm.h"
#include "../../include/syscall/errno.h"
#include <string.h>

#define Tversion 100
#define Tattach  104
#define Rerror   107
#define Twalk    110
#define Topen    112
#define Tread    116
#define Twrite   118
#define Tclunk   120
#define Tstat    124

#define NOFID 0xffffffffu
#define P9_MSIZE 8192

extern void task_sleep_ms(uint64_t ms);

typedef struct {
    tcp_tcb_t *tcb;
    uint32_t   msize;
    uint32_t   next_fid;
    spinlock_t slock;
    volatile int busy;
    uint8_t   *wbuf;
    uint8_t   *rbuf;
    int        refs;
} p9_conn_t;

typedef struct {
    p9_conn_t *conn;
    uint32_t   fid;
    int        is_dir;
    uint64_t   length;
    int        opened;
} p9_node_t;

static const vnode_ops_t p9_ops;

static void p9_lock(p9_conn_t *c) {
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&c->slock);
        if (!c->busy) { c->busy = 1; spinlock_release_irqrestore(&c->slock, f); return; }
        spinlock_release_irqrestore(&c->slock, f);
        task_sleep_ms(1);
    }
}
static void p9_unlock(p9_conn_t *c) {
    uint64_t f = spinlock_acquire_irqsave(&c->slock);
    c->busy = 0;
    spinlock_release_irqrestore(&c->slock, f);
}

static void p16(uint8_t *b, int *i, uint16_t v) { b[(*i)++] = v; b[(*i)++] = v >> 8; }
static void p32(uint8_t *b, int *i, uint32_t v) { for (int k = 0; k < 4; k++) b[(*i)++] = v >> (8 * k); }
static void p64(uint8_t *b, int *i, uint64_t v) { for (int k = 0; k < 8; k++) b[(*i)++] = v >> (8 * k); }
static void pstr(uint8_t *b, int *i, const char *s) { int n = strlen(s); p16(b, i, n); memcpy(b + *i, s, n); *i += n; }
static uint16_t g16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t g32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint64_t g64(const uint8_t *b) { uint64_t v = 0; for (int k = 0; k < 8; k++) v |= (uint64_t)b[k] << (8 * k); return v; }

static int send_full(p9_conn_t *c, const uint8_t *b, int n) {
    int off = 0;
    while (off < n) { int64_t r = tcp_send(c->tcb, b + off, n - off); if (r <= 0) return -1; off += r; }
    return 0;
}
static int recv_full(p9_conn_t *c, uint8_t *b, int n) {
    int off = 0;
    while (off < n) { int64_t r = tcp_recv(c->tcb, b + off, n - off, 0); if (r <= 0) return -1; off += r; }
    return 0;
}

static int p9_rpc(p9_conn_t *c, int len) {
    int z = 0; p32(c->wbuf, &z, (uint32_t)len);
    if (send_full(c, c->wbuf, len)) return -1;
    uint8_t hdr[4];
    if (recv_full(c, hdr, 4)) return -1;
    uint32_t sz = g32(hdr);
    if (sz < 7 || sz - 4 > c->msize) return -1;
    if (recv_full(c, c->rbuf, sz - 4)) return -1;
    if (c->rbuf[0] == Rerror) return -2;
    return (int)(sz - 4);
}

static int p9_version(p9_conn_t *c) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Tversion; m[5] = 0xff; m[6] = 0xff;
    p32(m, &i, P9_MSIZE); pstr(m, &i, "9P2000");
    int rl = p9_rpc(c, i);
    if (rl >= 7) { c->msize = g32(c->rbuf + 3); if (c->msize > P9_MSIZE || c->msize < 512) c->msize = P9_MSIZE; }
    p9_unlock(c);
    return rl < 0 ? -1 : 0;
}

static int p9_attach(p9_conn_t *c, uint32_t fid) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Tattach; m[5] = 0; m[6] = 0;
    p32(m, &i, fid); p32(m, &i, NOFID); pstr(m, &i, "cervus"); pstr(m, &i, "");
    int rl = p9_rpc(c, i);
    p9_unlock(c);
    return rl < 0 ? -1 : 0;
}

static int p9_walk(p9_conn_t *c, uint32_t from, uint32_t to, const char *name) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Twalk; m[5] = 1; m[6] = 0;
    p32(m, &i, from); p32(m, &i, to);
    if (name) { p16(m, &i, 1); pstr(m, &i, name); } else p16(m, &i, 0);
    int rl = p9_rpc(c, i);
    int nwqid = (rl >= 5) ? g16(c->rbuf + 3) : -1;
    p9_unlock(c);
    return (rl >= 0 && nwqid == (name ? 1 : 0)) ? 0 : -1;
}

static int p9_open(p9_conn_t *c, uint32_t fid, int mode) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Topen; m[5] = 2; m[6] = 0; p32(m, &i, fid); m[i++] = (uint8_t)mode;
    int rl = p9_rpc(c, i);
    p9_unlock(c);
    return rl < 0 ? -1 : 0;
}

static int p9_stat(p9_conn_t *c, uint32_t fid, int *is_dir, uint64_t *length) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Tstat; m[5] = 3; m[6] = 0; p32(m, &i, fid);
    int rl = p9_rpc(c, i);
    int ret = -1;
    if (rl >= 4 + 2 + 2 + 39) {
        const uint8_t *st = c->rbuf + 3 + 2 + 2;
        uint32_t mode = g32(st + 19);
        if (is_dir) *is_dir = (mode & 0x80000000u) ? 1 : 0;
        if (length) *length = g64(st + 31);
        ret = 0;
    }
    p9_unlock(c);
    return ret;
}

static void p9_clunk(p9_conn_t *c, uint32_t fid) {
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Tclunk; m[5] = 4; m[6] = 0; p32(m, &i, fid);
    p9_rpc(c, i);
    p9_unlock(c);
}

static long p9_read(p9_conn_t *c, uint32_t fid, uint64_t off, uint8_t *out, uint32_t cnt) {
    if (cnt > c->msize - 24) cnt = c->msize - 24;
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Tread; m[5] = 5; m[6] = 0; p32(m, &i, fid); p64(m, &i, off); p32(m, &i, cnt);
    int rl = p9_rpc(c, i);
    long ret = -1;
    if (rl >= 7) { uint32_t n = g32(c->rbuf + 3); if (n > (uint32_t)(rl - 7)) n = rl - 7; memcpy(out, c->rbuf + 7, n); ret = n; }
    p9_unlock(c);
    return ret;
}

static long p9_write(p9_conn_t *c, uint32_t fid, uint64_t off, const uint8_t *data, uint32_t cnt) {
    if (cnt > c->msize - 24) cnt = c->msize - 24;
    p9_lock(c);
    uint8_t *m = c->wbuf; int i = 7;
    m[4] = Twrite; m[5] = 6; m[6] = 0; p32(m, &i, fid); p64(m, &i, off); p32(m, &i, cnt);
    memcpy(m + i, data, cnt); i += cnt;
    int rl = p9_rpc(c, i);
    long ret = (rl >= 7) ? (long)g32(c->rbuf + 3) : -1;
    p9_unlock(c);
    return ret;
}

static vnode_t *p9_make_vnode(p9_conn_t *c, uint32_t fid, int is_dir, uint64_t length) {
    p9_node_t *n = kzalloc(sizeof *n);
    if (!n) return NULL;
    n->conn = c; n->fid = fid; n->is_dir = is_dir; n->length = length; n->opened = 0;
    vnode_t *vn = kzalloc(sizeof *vn);
    if (!vn) { kfree(n); return NULL; }
    vn->type = is_dir ? VFS_NODE_DIR : VFS_NODE_FILE;
    vn->mode = is_dir ? 0755 : 0644;
    vn->size = length;
    vn->ops = &p9_ops;
    vn->fs_data = n;
    vn->refcount = 1;
    return vn;
}

static int p9_ensure_open(p9_node_t *n, int want_write) {
    if (n->opened) return 0;
    int mode = want_write ? 2 : 0;
    if (p9_open(n->conn, n->fid, mode) == 0) { n->opened = 1; return 0; }
    if (want_write && p9_open(n->conn, n->fid, 0) == 0) { n->opened = 1; return 0; }
    return -1;
}

static int p9_lookup_op(vnode_t *dir, const char *name, vnode_t **out) {
    p9_node_t *dn = dir->fs_data;
    p9_conn_t *c = dn->conn;
    uint32_t nf = __atomic_fetch_add(&c->next_fid, 1, __ATOMIC_RELAXED);
    if (p9_walk(c, dn->fid, nf, name) != 0) return -ENOENT;
    int is_dir = 0; uint64_t length = 0;
    p9_stat(c, nf, &is_dir, &length);
    vnode_t *vn = p9_make_vnode(c, nf, is_dir, length);
    if (!vn) { p9_clunk(c, nf); return -ENOMEM; }
    *out = vn;
    return 0;
}

static int p9_readdir_op(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    p9_node_t *dn = dir->fs_data;
    p9_conn_t *c = dn->conn;
    uint32_t tf = __atomic_fetch_add(&c->next_fid, 1, __ATOMIC_RELAXED);
    if (p9_walk(c, dn->fid, tf, NULL) != 0) return -EIO;
    if (p9_open(c, tf, 0) != 0) { p9_clunk(c, tf); return -EIO; }

    uint8_t *buf = kmalloc(c->msize);
    if (!buf) { p9_clunk(c, tf); return -ENOMEM; }
    uint64_t off = 0; uint64_t idx = 0; int found = -1;
    for (;;) {
        long n = p9_read(c, tf, off, buf, c->msize - 32);
        if (n <= 0) break;
        off += n;
        int i = 0;
        while (i + 2 <= n) {
            int sz = g16(buf + i);
            if (sz == 0 || i + 2 + sz > n) break;
            const uint8_t *st = buf + i + 2;
            if (idx == index) {
                uint32_t mode = g32(st + 19);
                uint16_t nlen = g16(st + 39);
                if (nlen >= VFS_MAX_NAME) nlen = VFS_MAX_NAME - 1;
                memcpy(out->d_name, st + 41, nlen); out->d_name[nlen] = 0;
                out->d_type = (mode & 0x80000000u) ? VFS_NODE_DIR : VFS_NODE_FILE;
                out->d_ino = g64(st + 6 + 5);
                found = 0;
            }
            idx++;
            i += 2 + sz;
        }
        if (found == 0) break;
    }
    kfree(buf);
    p9_clunk(c, tf);
    return found;
}

static int64_t p9_read_op(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    p9_node_t *n = node->fs_data;
    if (n->is_dir) return -EISDIR;
    if (p9_ensure_open(n, 0) != 0) return -EIO;
    return p9_read(n->conn, n->fid, offset, buf, (uint32_t)len);
}

static int64_t p9_write_op(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    p9_node_t *n = node->fs_data;
    if (n->is_dir) return -EISDIR;
    if (p9_ensure_open(n, 1) != 0) return -EIO;
    return p9_write(n->conn, n->fid, offset, buf, (uint32_t)len);
}

static int p9_stat_op(vnode_t *node, vfs_stat_t *out) {
    p9_node_t *n = node->fs_data;
    int is_dir = n->is_dir; uint64_t length = n->length;
    p9_stat(n->conn, n->fid, &is_dir, &length);
    n->is_dir = is_dir; n->length = length; node->size = length;
    memset(out, 0, sizeof *out);
    out->st_type = is_dir ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->st_size = length;
    out->st_mode = is_dir ? 0755 : 0644;
    return 0;
}

static void p9_ref_op(vnode_t *n) { (void)n; }
static void p9_unref_op(vnode_t *node) {
    p9_node_t *n = node->fs_data;
    if (n && n->fid != 0) p9_clunk(n->conn, n->fid);
    kfree(n);
    kfree(node);
}

static const vnode_ops_t p9_ops = {
    .read    = p9_read_op,
    .write   = p9_write_op,
    .lookup  = p9_lookup_op,
    .readdir = p9_readdir_op,
    .stat    = p9_stat_op,
    .ref     = p9_ref_op,
    .unref   = p9_unref_op,
};

vnode_t *ninep_mount(uint32_t ip, uint16_t port) {
    p9_conn_t *c = kzalloc(sizeof *c);
    if (!c) return NULL;
    c->msize = P9_MSIZE; c->next_fid = 1;
    c->wbuf = kmalloc(P9_MSIZE); c->rbuf = kmalloc(P9_MSIZE);
    if (!c->wbuf || !c->rbuf) { kfree(c->wbuf); kfree(c->rbuf); kfree(c); return NULL; }
    if (tcp_connect(ip, port, &c->tcb) != 0 || !c->tcb) { kfree(c->wbuf); kfree(c->rbuf); kfree(c); return NULL; }
    if (p9_version(c) != 0 || p9_attach(c, 0) != 0) { tcp_close(c->tcb); kfree(c->wbuf); kfree(c->rbuf); kfree(c); return NULL; }
    int is_dir = 1; uint64_t length = 0;
    p9_stat(c, 0, &is_dir, &length);
    return p9_make_vnode(c, 0, 1, length);
}
