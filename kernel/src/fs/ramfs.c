#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "../../include/fs/ramfs.h"
#include "../../include/fs/vfs.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"

typedef struct {
    char     *name;
    vnode_t  *node;
} ramfs_child_t;

typedef struct {
    uint8_t      **chunks;
    size_t         chunk_count;
    size_t         chunk_cap;

    ramfs_child_t *children;
    int            child_count;
    int            child_cap;

    char          *link_target;

    uint64_t ino;
} ramfs_node_t;

static void ramfs_free_chunks(ramfs_node_t *rn) {
    if (rn->chunks) {
        for (size_t i = 0; i < rn->chunk_count; i++)
            if (rn->chunks[i]) pmm_free(rn->chunks[i], 1);
        kfree(rn->chunks);
    }
    rn->chunks      = NULL;
    rn->chunk_count = 0;
    rn->chunk_cap   = 0;
}

static int ramfs_ensure_chunks(ramfs_node_t *rn, size_t need_chunks) {
    if (need_chunks <= rn->chunk_count) return 0;
    if (need_chunks > RAMFS_MAX_CHUNKS) return -EFBIG;

    if (need_chunks > rn->chunk_cap) {
        size_t newcap = rn->chunk_cap ? rn->chunk_cap * 2 : 8;
        while (newcap < need_chunks) newcap *= 2;
        uint8_t **nc = kmalloc(newcap * sizeof(uint8_t *));
        if (!nc) return -ENOMEM;
        for (size_t i = 0; i < rn->chunk_count; i++) nc[i] = rn->chunks[i];
        for (size_t i = rn->chunk_count; i < newcap; i++) nc[i] = NULL;
        if (rn->chunks) kfree(rn->chunks);
        rn->chunks    = nc;
        rn->chunk_cap = newcap;
    }

    for (size_t i = rn->chunk_count; i < need_chunks; i++) {
        uint8_t *blk = pmm_alloc_zero(1);
        if (!blk) return -ENOMEM;
        rn->chunks[i] = blk;
    }
    rn->chunk_count = need_chunks;
    return 0;
}

static uint64_t g_next_ino = 1;
static const vnode_ops_t ramfs_file_ops;
static const vnode_ops_t ramfs_dir_ops;


static vnode_t *ramfs_alloc_vnode(vnode_type_t type, uint32_t mode) {
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return NULL;

    ramfs_node_t *rn = kzalloc(sizeof(ramfs_node_t));
    if (!rn) { kfree(v); return NULL; }

    rn->ino    = g_next_ino++;
    v->type    = type;
    v->mode    = mode;
    v->ino     = rn->ino;
    v->fs_data = rn;
    return v;
}

static void ramfs_ref(vnode_t *node) {
    (void)node;
}

static void ramfs_unref(vnode_t *node) {
    ramfs_node_t *rn = node->fs_data;

    ramfs_free_chunks(rn);
    if (rn->link_target) kfree(rn->link_target);

    if (rn->children) {
        for (int i = 0; i < rn->child_count; i++) {
            if (rn->children[i].name) kfree(rn->children[i].name);
            if (rn->children[i].node) vnode_unref(rn->children[i].node);
        }
        kfree(rn->children);
    }

    kfree(rn);
    kfree(node);
}

static int64_t ramfs_file_read(vnode_t *node, void *buf,
                                size_t len, uint64_t offset)
{
    ramfs_node_t *rn = node->fs_data;
    if (offset >= node->size) return 0;
    size_t avail = node->size - (size_t)offset;
    if (len > avail) len = avail;
    if (len == 0) return 0;

    uint8_t *dst = buf;
    size_t pos = (size_t)offset;
    size_t done = 0;
    while (done < len) {
        size_t ci  = pos / RAMFS_CHUNK_SIZE;
        size_t off = pos % RAMFS_CHUNK_SIZE;
        size_t n   = RAMFS_CHUNK_SIZE - off;
        if (n > len - done) n = len - done;
        if (ci < rn->chunk_count && rn->chunks[ci])
            memcpy(dst + done, rn->chunks[ci] + off, n);
        else
            memset(dst + done, 0, n);
        done += n;
        pos  += n;
    }
    return (int64_t)len;
}

static int64_t ramfs_file_write(vnode_t *node, const void *buf,
                                 size_t len, uint64_t offset)
{
    ramfs_node_t *rn = node->fs_data;
    if (len == 0) return 0;
    size_t end = (size_t)offset + len;
    if (end < (size_t)offset) return -EFBIG;

    size_t need_chunks = (end + RAMFS_CHUNK_SIZE - 1) / RAMFS_CHUNK_SIZE;
    int r = ramfs_ensure_chunks(rn, need_chunks);
    if (r < 0) return r;

    const uint8_t *src = buf;
    size_t pos = (size_t)offset;
    size_t done = 0;
    while (done < len) {
        size_t ci  = pos / RAMFS_CHUNK_SIZE;
        size_t off = pos % RAMFS_CHUNK_SIZE;
        size_t n   = RAMFS_CHUNK_SIZE - off;
        if (n > len - done) n = len - done;
        memcpy(rn->chunks[ci] + off, src + done, n);
        done += n;
        pos  += n;
    }
    if (end > node->size) node->size = end;
    return (int64_t)len;
}

static int ramfs_file_truncate(vnode_t *node, uint64_t new_size) {
    ramfs_node_t *rn = node->fs_data;
    if (new_size == 0) {
        ramfs_free_chunks(rn);
        node->size = 0;
    } else if (new_size < node->size) {
        size_t keep = (size_t)((new_size + RAMFS_CHUNK_SIZE - 1) / RAMFS_CHUNK_SIZE);
        for (size_t i = keep; i < rn->chunk_count; i++) {
            if (rn->chunks[i]) { pmm_free(rn->chunks[i], 1); rn->chunks[i] = NULL; }
        }
        if (keep < rn->chunk_count) rn->chunk_count = keep;
        node->size = new_size;
    }
    return 0;
}

static int ramfs_stat(vnode_t *node, vfs_stat_t *out) {
    ramfs_node_t *rn = node->fs_data;
    out->st_ino    = rn->ino;
    out->st_type   = node->type;
    out->st_mode   = node->mode;
    out->st_uid    = node->uid;
    out->st_gid    = node->gid;
    out->st_size   = node->size;
    out->st_blocks = (node->size + 511) / 512;
    return 0;
}

static const vnode_ops_t ramfs_file_ops = {
    .read     = ramfs_file_read,
    .write    = ramfs_file_write,
    .truncate = ramfs_file_truncate,
    .stat     = ramfs_stat,
    .ref      = ramfs_ref,
    .unref    = ramfs_unref,
};

static int ramfs_dir_grow(ramfs_node_t *rn) {
    int newcap = rn->child_cap == 0 ? 8 : rn->child_cap * 2;
    if (newcap > RAMFS_MAX_CHILDREN) return -ENOSPC;
    ramfs_child_t *nb = kmalloc((size_t)newcap * sizeof(ramfs_child_t));
    if (!nb) return -ENOMEM;
    if (rn->children && rn->child_count > 0)
        memcpy(nb, rn->children, (size_t)rn->child_count * sizeof(ramfs_child_t));
    if (rn->children) kfree(rn->children);
    rn->children  = nb;
    rn->child_cap = newcap;
    return 0;
}

static int ramfs_dir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    ramfs_node_t *rn = dir->fs_data;
    for (int i = 0; i < rn->child_count; i++) {
        if (strcmp(rn->children[i].name, name) == 0) {
            vnode_ref(rn->children[i].node);
            *out = rn->children[i].node;
            return 0;
        }
    }
    return -ENOENT;
}

static int ramfs_dir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    ramfs_node_t *rn = dir->fs_data;
    if ((int64_t)index >= rn->child_count) return -ENOENT;
    ramfs_child_t *ch = &rn->children[index];
    out->d_ino  = ch->node->ino;
    out->d_type = (uint8_t)ch->node->type;
    strncpy(out->d_name, ch->name, VFS_MAX_NAME - 1);
    out->d_name[VFS_MAX_NAME - 1] = '\0';
    return 0;
}

static int ramfs_dir_add_child(vnode_t *dir, const char *name, vnode_t *child) {
    ramfs_node_t *rn = dir->fs_data;

    if (rn->child_count >= rn->child_cap) {
        int r = ramfs_dir_grow(rn);
        if (r < 0) return r;
    }

    char *dup = kmalloc(strlen(name) + 1);
    if (!dup) return -ENOMEM;
    strcpy(dup, name);

    rn->children[rn->child_count].name = dup;
    rn->children[rn->child_count].node = child;
    rn->child_count++;
    dir->size = (uint64_t)rn->child_count;

    vnode_ref(child);
    return 0;
}

static int ramfs_dir_mkdir(vnode_t *dir, const char *name, uint32_t mode) {
    vnode_t *existing = NULL;
    if (ramfs_dir_lookup(dir, name, &existing) == 0) {
        vnode_unref(existing);
        return -EEXIST;
    }

    vnode_t *child = ramfs_alloc_vnode(VFS_NODE_DIR, mode ? mode : 0755);
    if (!child) return -ENOMEM;

    child->ops      = &ramfs_dir_ops;
    child->refcount = 1;

    int ret = ramfs_dir_add_child(dir, name, child);
    if (ret < 0) {
        kfree(child->fs_data);
        kfree(child);
        return ret;
    }

    vnode_unref(child);
    return 0;
}

static int ramfs_dir_create(vnode_t *dir, const char *name, uint32_t mode, vnode_t **out) {
    vnode_t *existing = NULL;
    if (ramfs_dir_lookup(dir, name, &existing) == 0) {
        *out = existing;
        return 0;
    }

    vnode_t *child = ramfs_alloc_vnode(VFS_NODE_FILE, mode ? mode : 0644);
    if (!child) return -ENOMEM;

    child->ops      = &ramfs_file_ops;
    child->refcount = 1;

    int ret = ramfs_dir_add_child(dir, name, child);
    if (ret < 0) {
        kfree(child->fs_data);
        kfree(child);
        return ret;
    }

    *out = child;
    return 0;
}

static int ramfs_dir_unlink(vnode_t *dir, const char *name) {
    ramfs_node_t *rn = dir->fs_data;
    for (int i = 0; i < rn->child_count; i++) {
        if (strcmp(rn->children[i].name, name) == 0) {
            kfree(rn->children[i].name);
            vnode_unref(rn->children[i].node);
            for (int j = i; j < rn->child_count - 1; j++)
                rn->children[j] = rn->children[j + 1];
            rn->child_count--;
            dir->size = (uint64_t)rn->child_count;
            return 0;
        }
    }
    return -ENOENT;
}

static int64_t ramfs_readlink(vnode_t *node, char *buf, size_t bufsiz) {
    ramfs_node_t *rn = node->fs_data;
    if (!rn->link_target) return -EINVAL;
    size_t n = strlen(rn->link_target);
    if (n > bufsiz) n = bufsiz;
    memcpy(buf, rn->link_target, n);
    return (int64_t)n;
}

static const vnode_ops_t ramfs_symlink_ops = {
    .stat     = ramfs_stat,
    .ref      = ramfs_ref,
    .unref    = ramfs_unref,
    .readlink = ramfs_readlink,
};

static int ramfs_dir_symlink(vnode_t *dir, const char *name, const char *target) {
    vnode_t *existing = NULL;
    if (ramfs_dir_lookup(dir, name, &existing) == 0) {
        vnode_unref(existing);
        return -EEXIST;
    }

    vnode_t *child = ramfs_alloc_vnode(VFS_NODE_SYMLINK, 0777);
    if (!child) return -ENOMEM;

    ramfs_node_t *rn = child->fs_data;
    size_t tl = strlen(target);
    rn->link_target = kmalloc(tl + 1);
    if (!rn->link_target) { kfree(rn); kfree(child); return -ENOMEM; }
    memcpy(rn->link_target, target, tl + 1);

    child->ops      = &ramfs_symlink_ops;
    child->size     = tl;
    child->refcount = 1;

    int ret = ramfs_dir_add_child(dir, name, child);
    if (ret < 0) {
        kfree(rn->link_target);
        kfree(rn);
        kfree(child);
        return ret;
    }
    vnode_unref(child);
    return 0;
}

static const vnode_ops_t ramfs_dir_ops = {
    .lookup  = ramfs_dir_lookup,
    .readdir = ramfs_dir_readdir,
    .mkdir   = ramfs_dir_mkdir,
    .create  = ramfs_dir_create,
    .unlink  = ramfs_dir_unlink,
    .stat    = ramfs_stat,
    .ref     = ramfs_ref,
    .unref   = ramfs_unref,
    .symlink = ramfs_dir_symlink,
};

vnode_t *ramfs_create_root(void) {
    vnode_t *root = ramfs_alloc_vnode(VFS_NODE_DIR, 0755);
    if (!root) return NULL;

    root->ops      = &ramfs_dir_ops;
    root->refcount = 1;

    serial_writestring("[ramfs] root created\n");
    return root;
}