#ifndef PAGECACHE_H
#define PAGECACHE_H

#include <stdint.h>
#include <stddef.h>

struct vnode;

int    pagecache_get(struct vnode *vn, uint64_t index, uintptr_t *phys_out);
int    pagecache_peek(struct vnode *vn, uint64_t index, uintptr_t *phys_out);
void   pagecache_wrote(struct vnode *vn, uint64_t offset, const void *buf, size_t len);
void   pagecache_truncated(struct vnode *vn, uint64_t new_size);
void   pagecache_invalidate(struct vnode *vn);
void   pagecache_invalidate_key(uintptr_t fs, uint64_t id);
void   pagecache_vnode_released(struct vnode *vn);
size_t pagecache_shrink(size_t want);
size_t pagecache_pages(void);
void   pagecache_init(void);

#endif
