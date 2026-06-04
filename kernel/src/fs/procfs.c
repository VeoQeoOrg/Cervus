#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../../include/fs/vfs.h"
#include "../../include/fs/procfs.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/smp.h"
#include "../../include/apic/apic.h"
#include "../../include/drivers/timer.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"

#define PROC_BUF_MAX 4096

typedef int (*proc_gen_fn)(char *buf, size_t max);

typedef struct {
    const char *name;
    proc_gen_fn gen;
    vnode_t     vnode;
} proc_file_t;

static int gen_meminfo(char *buf, size_t max) {
    uint64_t total = (uint64_t)pmm_get_usable_pages() * 4096;
    uint64_t used  = (uint64_t)pmm_get_used_pages()   * 4096;
    uint64_t free  = (uint64_t)pmm_get_free_pages()   * 4096;
    return snprintf(buf, max,
        "MemTotal:   %llu kB\n"
        "MemFree:    %llu kB\n"
        "MemUsed:    %llu kB\n"
        "PageSize:   4096 B\n",
        (unsigned long long)(total / 1024),
        (unsigned long long)(free / 1024),
        (unsigned long long)(used / 1024));
}

static int gen_cpuinfo(char *buf, size_t max) {
    uint32_t total  = smp_get_cpu_count();
    uint32_t online = smp_get_online_count();
    return snprintf(buf, max,
        "cpus:       %u\n"
        "online:     %u\n"
        "arch:       x86_64\n",
        total, online);
}

static int gen_uptime(char *buf, size_t max) {
    uint64_t ns = sched_now_ns();
    uint64_t sec = ns / 1000000000ULL;
    uint64_t ms  = (ns / 1000000ULL) % 1000ULL;
    return snprintf(buf, max, "%llu.%02llu\n",
        (unsigned long long)sec, (unsigned long long)(ms / 10));
}

static int gen_version(char *buf, size_t max) {
    return snprintf(buf, max, "Cervus OS x86_64\n");
}

static int gen_mounts(char *buf, size_t max) {
    vfs_mount_info_t mi[VFS_MAX_MOUNTS];
    int n = vfs_list_mounts(mi, VFS_MAX_MOUNTS);
    size_t off = 0;
    for (int i = 0; i < n && off < max; i++) {
        const char *dev = mi[i].device[0] ? mi[i].device : "none";
        int w = snprintf(buf + off, max - off, "%s %s %s 0 0\n",
                         dev, mi[i].path, mi[i].fstype);
        if (w < 0) break;
        off += (size_t)w;
    }
    return (int)off;
}

static int gen_loadavg(char *buf, size_t max) {
    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    int runnable = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (t && (t->state == TASK_RUNNING || t->state == TASK_READY)) runnable++;
    }
    return snprintf(buf, max, "%d.00 %d.00 %d.00 %d/%d 0\n",
                    runnable, runnable, runnable, runnable, n);
}

static proc_file_t g_proc_files[] = {
    { "meminfo", gen_meminfo, {0} },
    { "cpuinfo", gen_cpuinfo, {0} },
    { "uptime",  gen_uptime,  {0} },
    { "loadavg", gen_loadavg, {0} },
    { "version", gen_version, {0} },
    { "mounts",  gen_mounts,  {0} },
};
#define PROC_NFILES (sizeof(g_proc_files) / sizeof(g_proc_files[0]))

static vnode_t g_proc_root;
static uint64_t g_proc_ino = 1;

static int64_t procfile_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    proc_file_t *pf = (proc_file_t *)node->fs_data;
    char tmp[PROC_BUF_MAX];
    int total = pf->gen(tmp, sizeof(tmp));
    if (total < 0) total = 0;
    if ((size_t)total > sizeof(tmp)) total = sizeof(tmp);
    if (offset >= (uint64_t)total) return 0;
    size_t avail = (size_t)total - (size_t)offset;
    if (len > avail) len = avail;
    memcpy(buf, tmp + offset, len);
    return (int64_t)len;
}

static int procfile_stat(vnode_t *node, vfs_stat_t *out) {
    proc_file_t *pf = (proc_file_t *)node->fs_data;
    char tmp[PROC_BUF_MAX];
    int total = pf->gen(tmp, sizeof(tmp));
    if (total < 0) total = 0;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_FILE;
    out->st_mode = 0444;
    out->st_size = (uint64_t)total;
    return 0;
}

static void proc_ref(vnode_t *n)   { (void)n; }
static void proc_unref(vnode_t *n) { (void)n; }

static const vnode_ops_t procfile_ops = {
    .read  = procfile_read,
    .stat  = procfile_stat,
    .ref   = proc_ref,
    .unref = proc_unref,
};

#include "../../include/memory/vmm.h"

static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_RUNNING: return "running";
        case TASK_READY:   return "ready";
        case TASK_BLOCKED: return "sleeping";
        case TASK_ZOMBIE:  return "zombie";
        default:           return "dead";
    }
}

static int gen_pid_status(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    uint64_t rss = (t->is_userspace && t->pagemap)
                 ? vmm_count_user_pages(t->pagemap) * 4096 : 0;
    uint64_t vsz = (t->brk_max > t->brk_start) ? (t->brk_max - t->brk_start) : 0;
    return snprintf(buf, max,
        "Name:    %s\n"
        "Pid:     %u\n"
        "PPid:    %u\n"
        "PGid:    %u\n"
        "Sid:     %u\n"
        "Uid:     %u\n"
        "Gid:     %u\n"
        "State:   %s\n"
        "Prio:    %d\n"
        "VmSize:  %llu kB\n"
        "VmRSS:   %llu kB\n"
        "Runtime: %llu ns\n",
        t->name, t->pid, t->ppid, t->pgid, t->sid, t->uid, t->gid,
        state_name(t->state), t->priority,
        (unsigned long long)(vsz / 1024),
        (unsigned long long)(rss / 1024),
        (unsigned long long)t->total_runtime);
}

static int gen_pid_cmdline(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    return snprintf(buf, max, "%s\n", t->name);
}

static int gen_pid_stat(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    return snprintf(buf, max, "%u (%s) %c %u %u %u\n",
        t->pid, t->name, state_name(t->state)[0],
        t->ppid, t->pgid, t->sid);
}

static const char *vnode_type_name(int type) {
    switch (type) {
        case VFS_NODE_FILE:    return "file";
        case VFS_NODE_DIR:     return "dir";
        case VFS_NODE_CHARDEV: return "chardev";
        case VFS_NODE_BLKDEV:  return "blkdev";
        case VFS_NODE_SYMLINK: return "symlink";
        case VFS_NODE_PIPE:    return "pipe";
        default:               return "?";
    }
}

static int gen_pid_fd(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t || !t->fd_table) return -1;
    size_t off = 0;
    for (int fd = 0; fd < TASK_MAX_FDS && off < max; fd++) {
        int type = -1, oflags = 0;
        if (vfs_fd_info(t->fd_table, fd, &type, &oflags) < 0) continue;
        const char *acc = ((oflags & 3) == 0) ? "r" : ((oflags & 3) == 1) ? "w" : "rw";
        int w = snprintf(buf + off, max - off, "%d %s %s\n",
                         fd, vnode_type_name(type), acc);
        if (w < 0) break;
        off += (size_t)w;
    }
    return (int)off;
}

typedef int (*proc_pid_gen_fn)(uint32_t pid, char *buf, size_t max);

typedef struct { const char *name; proc_pid_gen_fn gen; } proc_pid_file_t;

static const proc_pid_file_t g_pid_files[] = {
    { "status",  gen_pid_status  },
    { "cmdline", gen_pid_cmdline },
    { "stat",    gen_pid_stat    },
    { "fd",      gen_pid_fd      },
};
#define PROC_PID_NFILES (sizeof(g_pid_files) / sizeof(g_pid_files[0]))

typedef struct {
    uint32_t        pid;
    proc_pid_gen_fn gen;
} proc_pid_data_t;

static int64_t pidfile_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    proc_pid_data_t *pd = (proc_pid_data_t *)node->fs_data;
    char tmp[PROC_BUF_MAX];
    int total = pd->gen(pd->pid, tmp, sizeof(tmp));
    if (total < 0) return -ESRCH;
    if ((size_t)total > sizeof(tmp)) total = sizeof(tmp);
    if (offset >= (uint64_t)total) return 0;
    size_t avail = (size_t)total - (size_t)offset;
    if (len > avail) len = avail;
    memcpy(buf, tmp + offset, len);
    return (int64_t)len;
}

static int pidfile_stat(vnode_t *node, vfs_stat_t *out) {
    proc_pid_data_t *pd = (proc_pid_data_t *)node->fs_data;
    char tmp[PROC_BUF_MAX];
    int total = pd->gen(pd->pid, tmp, sizeof(tmp));
    if (total < 0) total = 0;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_FILE;
    out->st_mode = 0444;
    out->st_size = (uint64_t)total;
    return 0;
}

static void piddyn_unref(vnode_t *node) {
    if (node->fs_data) kfree(node->fs_data);
    kfree(node);
}

static const vnode_ops_t pidfile_ops = {
    .read  = pidfile_read,
    .stat  = pidfile_stat,
    .ref   = proc_ref,
    .unref = piddyn_unref,
};

static int piddir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    uint32_t pid = (uint32_t)(uintptr_t)dir->fs_data;
    for (size_t i = 0; i < PROC_PID_NFILES; i++) {
        if (strcmp(g_pid_files[i].name, name) != 0) continue;
        vnode_t *v = kzalloc(sizeof(vnode_t));
        if (!v) return -ENOMEM;
        proc_pid_data_t *pd = kzalloc(sizeof(proc_pid_data_t));
        if (!pd) { kfree(v); return -ENOMEM; }
        pd->pid = pid;
        pd->gen = g_pid_files[i].gen;
        v->type = VFS_NODE_FILE;
        v->mode = 0444;
        v->ino  = g_proc_ino++;
        v->ops  = &pidfile_ops;
        v->fs_data = pd;
        v->refcount = 1;
        *out = v;
        return 0;
    }
    return -ENOENT;
}

static int piddir_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (index >= PROC_PID_NFILES) return -ENOENT;
    out->d_ino  = index + 1;
    out->d_type = (uint8_t)VFS_NODE_FILE;
    strncpy(out->d_name, g_pid_files[index].name, VFS_MAX_NAME - 1);
    out->d_name[VFS_MAX_NAME - 1] = '\0';
    return 0;
}

static int piddir_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_DIR;
    out->st_mode = 0555;
    out->st_size = PROC_PID_NFILES;
    return 0;
}

static void piddir_unref(vnode_t *node) {
    kfree(node);
}

static const vnode_ops_t piddir_ops = {
    .lookup  = piddir_lookup,
    .readdir = piddir_readdir,
    .stat    = piddir_stat,
    .ref     = proc_ref,
    .unref   = piddir_unref,
};

static int parse_pid(const char *s, uint32_t *out) {
    if (!s || !s[0]) return -1;
    uint32_t v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (uint32_t)(*p - '0');
    }
    *out = v;
    return 0;
}

static int make_piddir(uint32_t pid, vnode_t **out) {
    if (!task_find_by_pid(pid)) return -ENOENT;
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return -ENOMEM;
    v->type = VFS_NODE_DIR;
    v->mode = 0555;
    v->ino  = g_proc_ino++;
    v->ops  = &piddir_ops;
    v->fs_data = (void *)(uintptr_t)pid;
    v->refcount = 1;
    *out = v;
    return 0;
}

static int procroot_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    (void)dir;
    for (size_t i = 0; i < PROC_NFILES; i++) {
        if (strcmp(g_proc_files[i].name, name) == 0) {
            vnode_ref(&g_proc_files[i].vnode);
            *out = &g_proc_files[i].vnode;
            return 0;
        }
    }
    if (strcmp(name, "self") == 0) {
        task_t *me = current_task[lapic_get_id()];
        if (me) return make_piddir(me->pid, out);
        return -ENOENT;
    }
    uint32_t pid;
    if (parse_pid(name, &pid) == 0)
        return make_piddir(pid, out);
    return -ENOENT;
}

static int procroot_readdir(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (index < PROC_NFILES) {
        out->d_ino  = g_proc_files[index].vnode.ino;
        out->d_type = (uint8_t)VFS_NODE_FILE;
        strncpy(out->d_name, g_proc_files[index].name, VFS_MAX_NAME - 1);
        out->d_name[VFS_MAX_NAME - 1] = '\0';
        return 0;
    }
    uint32_t pids[256];
    int npids = task_collect_pids(pids, 256);
    uint64_t pidx = index - PROC_NFILES;
    if (pidx >= (uint64_t)npids) return -ENOENT;
    out->d_ino  = pids[pidx];
    out->d_type = (uint8_t)VFS_NODE_DIR;
    snprintf(out->d_name, VFS_MAX_NAME, "%u", pids[pidx]);
    return 0;
}

static int procroot_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_DIR;
    out->st_mode = 0555;
    out->st_size = PROC_NFILES;
    return 0;
}

static const vnode_ops_t procroot_ops = {
    .lookup  = procroot_lookup,
    .readdir = procroot_readdir,
    .stat    = procroot_stat,
    .ref     = proc_ref,
    .unref   = proc_unref,
};

vnode_t *procfs_create_root(void) {
    memset(&g_proc_root, 0, sizeof(g_proc_root));
    g_proc_root.type     = VFS_NODE_DIR;
    g_proc_root.mode     = 0555;
    g_proc_root.ino      = g_proc_ino++;
    g_proc_root.ops      = &procroot_ops;
    g_proc_root.refcount = 1;

    for (size_t i = 0; i < PROC_NFILES; i++) {
        vnode_t *v = &g_proc_files[i].vnode;
        memset(v, 0, sizeof(*v));
        v->type     = VFS_NODE_FILE;
        v->mode     = 0444;
        v->ino      = g_proc_ino++;
        v->ops      = &procfile_ops;
        v->fs_data  = &g_proc_files[i];
        v->refcount = 1;
    }

    serial_writestring("[procfs] /proc mounted (meminfo,cpuinfo,uptime,version,mounts,<pid>/,self)\n");
    return &g_proc_root;
}
