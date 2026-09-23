#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/memory/pmm.h"
#include <string.h>
#include <stdlib.h>

#define MEMFD_MAX_PAGES 8192

typedef struct {
    void     **pages;
    size_t     npages;
    size_t     size;
    char       name[32];
    spinlock_t lock;
} memfd_t;

static const vnode_ops_t MEMFD_OPS;

int memfd_is(const vnode_t *n)
{
    return n && n->ops == &MEMFD_OPS;
}

static int memfd_grow(memfd_t *m, size_t want_pages)
{
    if (want_pages > MEMFD_MAX_PAGES) return -ENOMEM;
    if (want_pages <= m->npages) return 0;

    void **np = calloc(want_pages, sizeof(void *));
    if (!np) return -ENOMEM;
    if (m->pages) {
        memcpy(np, m->pages, m->npages * sizeof(void *));
        free(m->pages);
    }
    m->pages = np;

    for (size_t i = m->npages; i < want_pages; i++) {
        void *pg = pmm_alloc_zero(1);
        if (!pg) {
            m->npages = i;
            return -ENOMEM;
        }
        m->pages[i] = pg;
    }
    m->npages = want_pages;
    return 0;
}

int memfd_page_phys(vnode_t *n, size_t index, uintptr_t *out)
{
    if (!memfd_is(n)) return -1;
    memfd_t *m = (memfd_t *)n->fs_data;

    uint64_t f = spinlock_acquire_irqsave(&m->lock);
    if (index >= m->npages && memfd_grow(m, index + 1) < 0) {
        spinlock_release_irqrestore(&m->lock, f);
        return -1;
    }
    void *pg = m->pages[index];
    if (index + 1 > (m->size + 0xFFF) / 0x1000) m->size = (index + 1) * 0x1000;
    spinlock_release_irqrestore(&m->lock, f);

    if (!pg) return -1;
    *out = pmm_virt_to_phys(pg);
    return 0;
}

static int64_t memfd_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    memfd_t *m = (memfd_t *)n->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&m->lock);

    if (off >= m->size) { spinlock_release_irqrestore(&m->lock, f); return 0; }
    if (off + len > m->size) len = m->size - off;

    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (done < len) {
        size_t page = (off + done) / 0x1000;
        size_t in   = (off + done) % 0x1000;
        size_t n_in = 0x1000 - in;
        if (n_in > len - done) n_in = len - done;
        if (page >= m->npages || !m->pages[page]) break;
        memcpy(dst + done, (uint8_t *)m->pages[page] + in, n_in);
        done += n_in;
    }
    spinlock_release_irqrestore(&m->lock, f);
    return (int64_t)done;
}

static int64_t memfd_write(vnode_t *n, const void *buf, size_t len, uint64_t off)
{
    memfd_t *m = (memfd_t *)n->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&m->lock);

    size_t need_pages = (off + len + 0xFFF) / 0x1000;
    if (need_pages > m->npages && memfd_grow(m, need_pages) < 0) {
        spinlock_release_irqrestore(&m->lock, f);
        return -ENOMEM;
    }

    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (done < len) {
        size_t page = (off + done) / 0x1000;
        size_t in   = (off + done) % 0x1000;
        size_t n_in = 0x1000 - in;
        if (n_in > len - done) n_in = len - done;
        if (page >= m->npages || !m->pages[page]) break;
        memcpy((uint8_t *)m->pages[page] + in, src + done, n_in);
        done += n_in;
    }
    if (off + done > m->size) m->size = off + done;
    spinlock_release_irqrestore(&m->lock, f);
    return (int64_t)done;
}

static int memfd_truncate(vnode_t *n, uint64_t new_size)
{
    memfd_t *m = (memfd_t *)n->fs_data;
    uint64_t f = spinlock_acquire_irqsave(&m->lock);
    int rc = memfd_grow(m, (size_t)((new_size + 0xFFF) / 0x1000));
    if (rc == 0) {
        if (new_size < m->size) {
            size_t keep = (size_t)new_size;
            size_t page = keep / 0x1000, in = keep % 0x1000;
            if (page < m->npages && m->pages[page] && in)
                memset((uint8_t *)m->pages[page] + in, 0, 0x1000 - in);
        }
        m->size = (size_t)new_size;
        n->size = new_size;
    }
    spinlock_release_irqrestore(&m->lock, f);
    return rc;
}

static int memfd_stat(vnode_t *n, vfs_stat_t *out)
{
    memfd_t *m = (memfd_t *)n->fs_data;
    if (!out) return -EINVAL;
    memset(out, 0, sizeof *out);
    out->st_size = (int64_t)m->size;
    out->st_mode = 0600;
    return 0;
}

static int memfd_poll(vnode_t *n, int events)
{
    (void)n;
    return events & (POLLIN | POLLOUT);
}

static void memfd_unref(vnode_t *n)
{
    if (--n->refcount > 0) return;
    memfd_t *m = (memfd_t *)n->fs_data;
    if (m) {
        for (size_t i = 0; i < m->npages; i++)
            if (m->pages[i]) pmm_free(m->pages[i], 1);
        free(m->pages);
        free(m);
    }
    free(n);
}

static const vnode_ops_t MEMFD_OPS = {
    .read     = memfd_read,
    .write    = memfd_write,
    .truncate = memfd_truncate,
    .stat     = memfd_stat,
    .poll     = memfd_poll,
    .unref    = memfd_unref,
};

int64_t sys_memfd_create(uint64_t name_ptr, uint64_t flags, uint64_t unused)
{
    (void)unused;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EINVAL;

    memfd_t *m = calloc(1, sizeof *m);
    if (!m) return -ENOMEM;
    m->lock = (spinlock_t)SPINLOCK_INIT;

    if (name_ptr) {
        char tmp[32];
        if (syscall_copy_from_user(tmp, (void *)name_ptr, sizeof tmp) == 0) {
            tmp[sizeof tmp - 1] = 0;
            memcpy(m->name, tmp, sizeof m->name);
        }
    }

    vnode_t *vn = calloc(1, sizeof *vn);
    if (!vn) { free(m); return -ENOMEM; }
    vn->type     = VFS_NODE_FILE;
    vn->mode     = 0600;
    vn->ops      = &MEMFD_OPS;
    vn->fs_data  = m;
    vn->refcount = 1;

    vfs_file_t *file = vfs_file_alloc();
    if (!file) { free(vn); free(m); return -ENOMEM; }
    file->vnode = vn;
    file->flags = 2;

    int fd = fd_alloc(t->fd_table, file, 0);
    if (fd < 0) { vfs_file_free(file); return -EMFILE; }
    if (flags & 1) fd_set_flags(t->fd_table, fd, FD_CLOEXEC);
    return fd;
}
