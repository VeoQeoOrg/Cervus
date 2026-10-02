#include "../../include/memory/pagecache.h"
#include "../../include/memory/pageref.h"
#include "../../include/memory/pmm.h"
#include "../../include/fs/vfs.h"
#include "../../include/sched/spinlock.h"
#include "../../include/io/serial.h"
#include <string.h>

#define PC_BUCKETS 4096

typedef struct pc_entry {
    struct pc_entry *hnext;
    struct pc_entry *lnext;
    struct pc_entry *lprev;
    uintptr_t        fs;
    uint64_t         id;
    uint64_t         index;
    uintptr_t        phys;
} pc_entry_t;

static pc_entry_t *g_hash[PC_BUCKETS];
static pc_entry_t *g_lru_head;
static pc_entry_t *g_lru_tail;
static size_t      g_count;
static spinlock_t  g_lock = SPINLOCK_INIT;

static bool key_of(vnode_t *vn, uintptr_t *fs, uint64_t *id) {
    if (vn->ops && vn->ops->cache_id && vn->ops->cache_id(vn, fs, id) == 0) return true;
    *fs = (uintptr_t)vn;
    *id = 0;
    return false;
}

static inline size_t bucket(uintptr_t fs, uint64_t id, uint64_t index) {
    uint64_t h = (uint64_t)fs * 0x9E3779B97F4A7C15ULL;
    h ^= id * 0xC2B2AE3D27D4EB4FULL;
    h ^= index * 0x165667B19E3779F9ULL;
    h ^= h >> 29;
    return (size_t)(h % PC_BUCKETS);
}

static pc_entry_t *find_locked(uintptr_t fs, uint64_t id, uint64_t index) {
    for (pc_entry_t *e = g_hash[bucket(fs, id, index)]; e; e = e->hnext)
        if (e->fs == fs && e->id == id && e->index == index) return e;
    return NULL;
}

static void lru_unlink(pc_entry_t *e) {
    if (e->lprev) e->lprev->lnext = e->lnext; else g_lru_head = e->lnext;
    if (e->lnext) e->lnext->lprev = e->lprev; else g_lru_tail = e->lprev;
    e->lnext = e->lprev = NULL;
}

static void lru_push_front(pc_entry_t *e) {
    e->lprev = NULL;
    e->lnext = g_lru_head;
    if (g_lru_head) g_lru_head->lprev = e; else g_lru_tail = e;
    g_lru_head = e;
}

static void hash_remove(pc_entry_t *e) {
    pc_entry_t **pp = &g_hash[bucket(e->fs, e->id, e->index)];
    while (*pp && *pp != e) pp = &(*pp)->hnext;
    if (*pp) *pp = e->hnext;
    lru_unlink(e);
    g_count--;
}

int pagecache_get(vnode_t *vn, uint64_t index, uintptr_t *phys_out) {
    if (!vn || !vn->ops || !vn->ops->read) return -EIO;
    uintptr_t fs; uint64_t id;
    key_of(vn, &fs, &id);

    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    pc_entry_t *e = find_locked(fs, id, index);
    if (e) {
        page_ref(e->phys);
        lru_unlink(e);
        lru_push_front(e);
        *phys_out = e->phys;
        spinlock_release_irqrestore(&g_lock, f);
        return 0;
    }
    spinlock_release_irqrestore(&g_lock, f);

    uintptr_t phys = page_alloc_user();
    if (!phys) return -ENOMEM;
    uint8_t *kva = (uint8_t *)pmm_phys_to_virt(phys);
    uint64_t off = index * PAGE_SIZE;
    size_t got = 0;
    while (got < PAGE_SIZE) {
        int64_t r = vn->ops->read(vn, kva + got, PAGE_SIZE - got, off + got);
        if (r < 0) { page_unref(phys); return (int)r; }
        if (r == 0) break;
        got += (size_t)r;
    }

    pc_entry_t *ne = kzalloc(sizeof(*ne));
    if (!ne) { *phys_out = phys; return 0; }
    ne->fs = fs; ne->id = id; ne->index = index; ne->phys = phys;

    f = spinlock_acquire_irqsave(&g_lock);
    e = find_locked(fs, id, index);
    if (e) {
        page_ref(e->phys);
        *phys_out = e->phys;
        spinlock_release_irqrestore(&g_lock, f);
        kfree(ne);
        page_unref(phys);
        return 0;
    }
    size_t b = bucket(fs, id, index);
    ne->hnext = g_hash[b];
    g_hash[b] = ne;
    lru_push_front(ne);
    g_count++;
    page_ref(phys);
    *phys_out = phys;
    spinlock_release_irqrestore(&g_lock, f);
    return 0;
}

int pagecache_peek(vnode_t *vn, uint64_t index, uintptr_t *phys_out) {
    if (!vn) return -ENOENT;
    uintptr_t fs; uint64_t id;
    key_of(vn, &fs, &id);
    uint64_t f = spinlock_acquire_irqsave(&g_lock);
    pc_entry_t *e = find_locked(fs, id, index);
    if (e) { page_ref(e->phys); *phys_out = e->phys; }
    spinlock_release_irqrestore(&g_lock, f);
    return e ? 0 : -ENOENT;
}

void pagecache_wrote(vnode_t *vn, uint64_t offset, const void *buf, size_t len) {
    if (!vn || !len) return;
    uintptr_t fs; uint64_t id;
    key_of(vn, &fs, &id);
    const uint8_t *src = buf;
    uint64_t end = offset + len;
    for (uint64_t pg = offset / PAGE_SIZE; pg * PAGE_SIZE < end; pg++) {
        uint64_t pstart = pg * PAGE_SIZE;
        uint64_t cs = offset > pstart ? offset : pstart;
        uint64_t ce = end < pstart + PAGE_SIZE ? end : pstart + PAGE_SIZE;
        uint64_t f = spinlock_acquire_irqsave(&g_lock);
        pc_entry_t *e = find_locked(fs, id, pg);
        uintptr_t phys = 0;
        if (e) { phys = e->phys; page_ref(phys); }
        spinlock_release_irqrestore(&g_lock, f);
        if (!phys) continue;
        memcpy((uint8_t *)pmm_phys_to_virt(phys) + (cs - pstart), src + (cs - offset), (size_t)(ce - cs));
        page_unref(phys);
    }
}

static void drop_matching(uintptr_t fs, uint64_t id, uint64_t from_index) {
    uintptr_t freed[64];
    for (;;) {
        int n = 0;
        uint64_t f = spinlock_acquire_irqsave(&g_lock);
        for (size_t b = 0; b < PC_BUCKETS && n < 64; b++) {
            pc_entry_t *e = g_hash[b];
            while (e && n < 64) {
                pc_entry_t *nx = e->hnext;
                if (e->fs == fs && e->id == id && e->index >= from_index) {
                    hash_remove(e);
                    freed[n++] = e->phys;
                    kfree(e);
                }
                e = nx;
            }
        }
        spinlock_release_irqrestore(&g_lock, f);
        for (int i = 0; i < n; i++) page_unref(freed[i]);
        if (n < 64) break;
    }
}

void pagecache_truncated(vnode_t *vn, uint64_t new_size) {
    if (!vn) return;
    uintptr_t fs; uint64_t id;
    key_of(vn, &fs, &id);
    uint64_t keep = (new_size + PAGE_SIZE - 1) / PAGE_SIZE;
    drop_matching(fs, id, keep);
    if (new_size % PAGE_SIZE) {
        uint64_t f = spinlock_acquire_irqsave(&g_lock);
        pc_entry_t *e = find_locked(fs, id, new_size / PAGE_SIZE);
        uintptr_t phys = 0;
        if (e) { phys = e->phys; page_ref(phys); }
        spinlock_release_irqrestore(&g_lock, f);
        if (phys) {
            size_t in = (size_t)(new_size % PAGE_SIZE);
            memset((uint8_t *)pmm_phys_to_virt(phys) + in, 0, PAGE_SIZE - in);
            page_unref(phys);
        }
    }
}

void pagecache_invalidate_key(uintptr_t fs, uint64_t id) {
    if (!g_count) return;
    drop_matching(fs, id, 0);
}

void pagecache_invalidate(vnode_t *vn) {
    if (!vn) return;
    uintptr_t fs; uint64_t id;
    key_of(vn, &fs, &id);
    drop_matching(fs, id, 0);
}

void pagecache_vnode_released(vnode_t *vn) {
    if (!vn) return;
    uintptr_t fs; uint64_t id;
    if (key_of(vn, &fs, &id)) return;
    if (!g_count) return;
    drop_matching(fs, id, 0);
}

size_t pagecache_shrink(size_t want) {
    size_t done = 0;
    while (done < want) {
        uintptr_t victim = 0;
        uint64_t f = spinlock_acquire_irqsave(&g_lock);
        for (pc_entry_t *e = g_lru_tail; e; e = e->lprev) {
            if (page_refcount(e->phys) == 1) {
                hash_remove(e);
                victim = e->phys;
                kfree(e);
                break;
            }
        }
        spinlock_release_irqrestore(&g_lock, f);
        if (!victim) break;
        page_unref(victim);
        done++;
    }
    return done;
}

size_t pagecache_pages(void) { return g_count; }

void pagecache_init(void) {
    page_set_shrinker(pagecache_shrink);
}
