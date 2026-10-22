#ifndef _KERNEL_FS_POLLWAIT_H
#define _KERNEL_FS_POLLWAIT_H

#include <stdint.h>
#include "../sched/waitq.h"

struct vfs_file;

typedef struct poll_entry {
    waitq_t *q;
    void   (*put)(void *);
    void    *obj;
} poll_entry_t;

typedef struct poll_table {
    void            (*qproc)(struct poll_table *pt, waitq_t *q, void (*put)(void *), void *obj);
    void             *qctx;
    int               noreg;
    int               n;
    int               cap;
    int               touched;
    int               legacy;
    uint64_t          deadline_ns;
    struct vfs_file  *cur_file;
    poll_entry_t     *e;
    poll_entry_t      inl[16];
} poll_table_t;

uint64_t poll_seq(void);
void     poll_notify(void);

void     poll_wait(waitq_t *q);
void     poll_wait_obj(waitq_t *q, void (*put)(void *), void *obj);
void     poll_deadline(uint64_t deadline_ns);
void     poll_mark_legacy(void);

void     poll_table_init(poll_table_t *pt);
uint64_t poll_table_begin(poll_table_t *pt);
void     poll_table_end(poll_table_t *pt);
struct vfs_file *poll_table_file(struct vfs_file *file);
void     poll_table_note(struct vfs_file *prev);
void     poll_table_release(poll_table_t *pt);
void     poll_table_block(poll_table_t *pt, uint64_t gseen, uint64_t wseen, uint64_t deadline_ns);

#endif
