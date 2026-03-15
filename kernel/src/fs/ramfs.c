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
    uint8_t       *data;
    size_t         capacity;

    ramfs_child_t *children;
    int            child_count;
    int            child_cap;

    uint64_t ino;
} ramfs_node_t;

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

    if (rn->data) kfree(rn->data);

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
    memcpy(buf, rn->data + offset, len);
    return (int64_t)len;
}

static int64_t ramfs_file_write(vnode_t *node, const void *buf,
                                 size_t len, uint64_t offset)
{
    ramfs_node_t *rn = node->fs_data;
    size_t end = (size_t)offset + len;
    if (end > RAMFS_MAX_FILE_SIZE) return -EFBIG;

    if (end > rn->capacity) {
        size_t newcap = (end + 4095) & ~(size_t)4095;
        if (newcap > RAMFS_MAX_FILE_SIZE) newcap = RAMFS_MAX_FILE_SIZE;
        uint8_t *newdata = kmalloc(newcap);
        if (!newdata) return -ENOMEM;

        size_t copy_size = rn->data ? node->size : 0;
        if (copy_size > 0)
            memcpy(newdata, rn->data, copy_size);
        memset(newdata + copy_size, 0, newcap - copy_size);

        if (rn->data) kfree(rn->data);
        rn->data     = newdata;
        rn->capacity = newcap;
    }

    memcpy(rn->data + offset, buf, len);
    if (end > node->size) node->size = end;
    return (int64_t)len;
}

static int ramfs_file_truncate(vnode_t *node, uint64_t new_size) {
    ramfs_node_t *rn = node->fs_data;
    if (new_size == 0) {
        if (rn->data) {
            kfree(rn->data);
            rn->data     = NULL;
            rn->capacity = 0;
        }
        node->size = 0;
    } else if (new_size < node->size) {
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

static const vnode_ops_t ramfs_dir_ops = {
    .lookup  = ramfs_dir_lookup,
    .readdir = ramfs_dir_readdir,
    .mkdir   = ramfs_dir_mkdir,
    .create  = ramfs_dir_create,
    .unlink  = ramfs_dir_unlink,
    .stat    = ramfs_stat,
    .ref     = ramfs_ref,
    .unref   = ramfs_unref,
};

vnode_t *ramfs_create_root(void) {
    vnode_t *root = ramfs_alloc_vnode(VFS_NODE_DIR, 0755);
    if (!root) return NULL;

    root->ops      = &ramfs_dir_ops;
    root->refcount = 1;

    serial_writestring("[ramfs] root created\n");
    return root;
}