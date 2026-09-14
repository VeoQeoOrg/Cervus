#include "../../include/fs/vfs.h"
#include "../../include/sched/spinlock.h"
#include <string.h>

#define VFS_MAX_LOCKS 256

typedef struct {
    bool     used;
    vnode_t *vnode;
    uint64_t start;
    uint64_t end;
    int      type;
    int      owner;
} file_lock_t;

static file_lock_t g_locks[VFS_MAX_LOCKS];
static spinlock_t  g_lock_table = SPINLOCK_INIT;

static bool overlaps(const file_lock_t *l, uint64_t start, uint64_t end)
{
    return l->start <= end && start <= l->end;
}

static int lock_insert(vnode_t *vnode, uint64_t start, uint64_t end, int type, int owner)
{
    for (int i = 0; i < VFS_MAX_LOCKS; i++) {
        if (g_locks[i].used) continue;
        g_locks[i].used   = true;
        g_locks[i].vnode  = vnode;
        g_locks[i].start  = start;
        g_locks[i].end    = end;
        g_locks[i].type   = type;
        g_locks[i].owner  = owner;
        return 0;
    }
    return -ENOLCK;
}

static int lock_clear_range(vnode_t *vnode, uint64_t start, uint64_t end, int owner)
{
    for (int i = 0; i < VFS_MAX_LOCKS; i++) {
        file_lock_t *l = &g_locks[i];
        if (!l->used || l->vnode != vnode || l->owner != owner) continue;
        if (!overlaps(l, start, end)) continue;

        uint64_t old_start = l->start, old_end = l->end;
        int old_type = l->type;
        l->used = false;

        if (old_start < start) {
            int r = lock_insert(vnode, old_start, start - 1, old_type, owner);
            if (r < 0) return r;
        }
        if (old_end > end) {
            int r = lock_insert(vnode, end + 1, old_end, old_type, owner);
            if (r < 0) return r;
        }
    }
    return 0;
}

int vfs_lock_test(vnode_t *vnode, int type, uint64_t start, uint64_t end,
                  int owner, vfs_flock_t *conflict)
{
    if (!vnode) return -EINVAL;
    uint64_t f = spinlock_acquire_irqsave(&g_lock_table);
    int r = 0;
    for (int i = 0; i < VFS_MAX_LOCKS; i++) {
        file_lock_t *l = &g_locks[i];
        if (!l->used || l->vnode != vnode || l->owner == owner) continue;
        if (!overlaps(l, start, end)) continue;
        if (type == VFS_LOCK_READ && l->type == VFS_LOCK_READ) continue;
        if (conflict) {
            conflict->type  = l->type;
            conflict->start = l->start;
            conflict->end   = l->end;
            conflict->owner = l->owner;
        }
        r = -EAGAIN;
        break;
    }
    spinlock_release_irqrestore(&g_lock_table, f);
    return r;
}

int vfs_lock_set(vnode_t *vnode, int type, uint64_t start, uint64_t end, int owner)
{
    if (!vnode) return -EINVAL;

    if (type != VFS_LOCK_UNLOCK) {
        int c = vfs_lock_test(vnode, type, start, end, owner, NULL);
        if (c < 0) return c;
    }

    uint64_t f = spinlock_acquire_irqsave(&g_lock_table);
    int r = lock_clear_range(vnode, start, end, owner);
    if (r == 0 && type != VFS_LOCK_UNLOCK)
        r = lock_insert(vnode, start, end, type, owner);
    spinlock_release_irqrestore(&g_lock_table, f);
    return r;
}

void vfs_lock_release_owner(int owner)
{
    uint64_t f = spinlock_acquire_irqsave(&g_lock_table);
    for (int i = 0; i < VFS_MAX_LOCKS; i++) {
        if (g_locks[i].used && g_locks[i].owner == owner)
            g_locks[i].used = false;
    }
    spinlock_release_irqrestore(&g_lock_table, f);
}

void vfs_lock_release_vnode(vnode_t *vnode, int owner)
{
    uint64_t f = spinlock_acquire_irqsave(&g_lock_table);
    for (int i = 0; i < VFS_MAX_LOCKS; i++) {
        if (g_locks[i].used && g_locks[i].vnode == vnode && g_locks[i].owner == owner)
            g_locks[i].used = false;
    }
    spinlock_release_irqrestore(&g_lock_table, f);
}
