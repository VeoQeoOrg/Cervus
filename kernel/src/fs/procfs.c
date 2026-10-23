#include "../../include/sched/ptrace.h"
#include "../../include/gdt/gdt.h"
#include "../../include/memory/mm.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../../include/fs/vfs.h"
#include "../../include/net/socket.h"
#include "../../include/net/tcp.h"
#include "../../include/net/arp.h"
#include "../../include/net/netdev.h"
#include "../../include/fs/procfs.h"
#include "../../include/memory/pmm.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/time/clocksource.h"
#include "../../include/apic/apic.h"
#include "../../include/drivers/timer.h"
#include "../../include/sched/sched.h"
#include "../../include/puzzle/puzzle.h"
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

static void cpuid_regs(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    asm volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}

typedef struct { uint8_t reg, bit; const char *name; } cpu_flag_t;

static const cpu_flag_t g_flags1[] = {
    {3,0,"fpu"},{3,1,"vme"},{3,2,"de"},{3,3,"pse"},{3,4,"tsc"},{3,5,"msr"},{3,6,"pae"},{3,7,"mce"},
    {3,8,"cx8"},{3,9,"apic"},{3,11,"sep"},{3,12,"mtrr"},{3,13,"pge"},{3,14,"mca"},{3,15,"cmov"},
    {3,16,"pat"},{3,17,"pse36"},{3,19,"clflush"},{3,23,"mmx"},{3,24,"fxsr"},{3,25,"sse"},{3,26,"sse2"},
    {3,28,"ht"},{2,0,"pni"},{2,1,"pclmulqdq"},{2,9,"ssse3"},{2,12,"fma"},{2,13,"cx16"},{2,19,"sse4_1"},
    {2,20,"sse4_2"},{2,22,"movbe"},{2,23,"popcnt"},{2,25,"aes"},{2,26,"xsave"},{2,28,"avx"},
    {2,29,"f16c"},{2,30,"rdrand"},{2,31,"hypervisor"},
};
static const cpu_flag_t g_flags7[] = {
    {1,0,"fsgsbase"},{1,3,"bmi1"},{1,5,"avx2"},{1,8,"bmi2"},{1,9,"erms"},{1,18,"rdseed"},{1,19,"adx"},
    {1,29,"sha_ni"},{1,16,"avx512f"},
};
static const cpu_flag_t g_flagsx[] = {
    {3,11,"syscall"},{3,20,"nx"},{3,27,"rdtscp"},{3,29,"lm"},{2,0,"lahf_lm"},{2,5,"abm"},
};

static int gen_cpuinfo(char *buf, size_t max) {
    uint32_t total  = smp_get_cpu_count();
    uint32_t online = smp_get_online_count();
    int off = snprintf(buf, max,
        "cpus:       %u\n"
        "online:     %u\n"
        "arch:       x86_64\n\n",
        total, online);
    uint32_t r[4], r7[4] = {0}, rx[4] = {0}, l0[4];
    char vendor[13], brand[49];
    cpuid_regs(0, 0, l0);
    memcpy(vendor, &l0[1], 4); memcpy(vendor + 4, &l0[3], 4); memcpy(vendor + 8, &l0[2], 4);
    vendor[12] = 0;
    cpuid_regs(1, 0, r);
    if (l0[0] >= 7) cpuid_regs(7, 0, r7);
    uint32_t ext[4];
    cpuid_regs(0x80000000u, 0, ext);
    if (ext[0] >= 0x80000001u) cpuid_regs(0x80000001u, 0, rx);
    memset(brand, 0, sizeof brand);
    if (ext[0] >= 0x80000004u)
        for (uint32_t i = 0; i < 3; i++) cpuid_regs(0x80000002u + i, 0, (uint32_t *)(brand + 16 * i));
    const char *b = brand;
    while (*b == ' ') b++;
    uint32_t family = (r[0] >> 8) & 0xF, model = (r[0] >> 4) & 0xF, stepping = r[0] & 0xF;
    if (family == 0xF) family += (r[0] >> 20) & 0xFF;
    if (family >= 6) model |= ((r[0] >> 16) & 0xF) << 4;
    char flags[640];
    size_t fl = 0;
    flags[0] = 0;
    for (size_t i = 0; i < sizeof g_flags1 / sizeof g_flags1[0]; i++)
        if (r[g_flags1[i].reg] & (1u << g_flags1[i].bit))
            fl += (size_t)snprintf(flags + fl, sizeof flags - fl, "%s ", g_flags1[i].name);
    for (size_t i = 0; i < sizeof g_flagsx / sizeof g_flagsx[0]; i++)
        if (rx[g_flagsx[i].reg] & (1u << g_flagsx[i].bit))
            fl += (size_t)snprintf(flags + fl, sizeof flags - fl, "%s ", g_flagsx[i].name);
    for (size_t i = 0; i < sizeof g_flags7 / sizeof g_flags7[0]; i++)
        if (r7[g_flags7[i].reg] & (1u << g_flags7[i].bit))
            fl += (size_t)snprintf(flags + fl, sizeof flags - fl, "%s ", g_flags7[i].name);
    if (fl) flags[fl - 1] = 0;
    uint64_t mhz = g_tsc_khz / 1000;
    for (uint32_t c = 0; c < online && off >= 0 && (size_t)off < max; c++) {
        off += snprintf(buf + off, max - (size_t)off,
            "processor\t: %u\nvendor_id\t: %s\ncpu family\t: %u\nmodel\t\t: %u\n"
            "model name\t: %s\nstepping\t: %u\ncpu MHz\t\t: %llu.000\ncache size\t: 512 KB\n"
            "physical id\t: 0\nsiblings\t: %u\ncore id\t\t: %u\ncpu cores\t: %u\napicid\t\t: %u\n"
            "fpu\t\t: yes\nfpu_exception\t: yes\ncpuid level\t: %u\nwp\t\t: yes\nflags\t\t: %s\n"
            "bogomips\t: %llu.00\nclflush size\t: 64\ncache_alignment\t: 64\n"
            "address sizes\t: 48 bits physical, 48 bits virtual\npower management:\n\n",
            c, vendor, family, model, *b ? b : "x86_64 CPU", stepping, (unsigned long long)mhz,
            online, c, online, c, l0[0], flags, (unsigned long long)(mhz * 2));
    }
    return off;
}

extern int64_t clock_realtime_offset_ns(void);

static int gen_stat(char *buf, size_t max) {
    uint32_t ncpu = smp_get_online_count();
    if (!ncpu) ncpu = 1;
    uint64_t up_t = sched_now_ns() / 10000000ULL;
    uint32_t pids[256];
    int n = task_collect_pids(pids, 256);
    uint64_t busy = 0;
    int running = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(pids[i]);
        if (!t) continue;
        busy += t->total_runtime / 10000000ULL;
        if (t->state == TASK_RUNNING || t->state == TASK_READY) running++;
    }
    uint64_t all = up_t * ncpu;
    uint64_t idle = all > busy ? all - busy : 0;
    int off = snprintf(buf, max, "cpu  %llu 0 0 %llu 0 0 0 0 0 0\n",
                       (unsigned long long)busy, (unsigned long long)idle);
    for (uint32_t c = 0; c < ncpu && off >= 0 && (size_t)off < max; c++)
        off += snprintf(buf + off, max - (size_t)off, "cpu%u %llu 0 0 %llu 0 0 0 0 0 0\n", c,
                        (unsigned long long)(busy / ncpu), (unsigned long long)(idle / ncpu));
    int64_t boot = clock_realtime_offset_ns() / 1000000000LL;
    if (off >= 0 && (size_t)off < max)
        off += snprintf(buf + off, max - (size_t)off,
                        "intr 0\nctxt 0\nbtime %lld\nprocesses %d\nprocs_running %d\nprocs_blocked 0\n",
                        (long long)boot, n, running ? running : 1);
    return off;
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
        int w = snprintf(buf + off, max - off, "%s %s %s rw 0 0\n",
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

static uint64_t g_drift_t0_tsc   = 0;
static uint64_t g_drift_t0_hpet  = 0;
static uint64_t g_drift_t0_ticks = 0;
static int      g_drift_armed    = 0;

static int64_t drift_ppm(uint64_t a, uint64_t b) {
    if (b == 0) return 0;
    int64_t diff = (int64_t)a - (int64_t)b;
    return (diff * 1000000) / (int64_t)b;
}

static int gen_timedrift(char *buf, size_t max) {
    uint64_t tsc   = tsc_elapsed_ns();
    uint64_t hpet  = hpet_is_available() ? hpet_elapsed_ns() : 0;
    uint64_t lapic = timer_get_ticks() * 1000000ULL;

    if (!g_drift_armed) {
        g_drift_t0_tsc   = tsc;
        g_drift_t0_hpet  = hpet;
        g_drift_t0_ticks = lapic;
        g_drift_armed    = 1;
        return snprintf(buf, max,
            "snapshot armed (tsc=%llu hpet=%llu lapic=%llu ns)\n"
            "read again later to see drift\n",
            (unsigned long long)tsc, (unsigned long long)hpet,
            (unsigned long long)lapic);
    }

    uint64_t d_tsc   = tsc   - g_drift_t0_tsc;
    uint64_t d_hpet  = hpet  - g_drift_t0_hpet;
    uint64_t d_lapic = lapic - g_drift_t0_ticks;

    uint64_t ref = hpet ? d_hpet : d_tsc;
    const char *refname = hpet ? "hpet" : "tsc";

    return snprintf(buf, max,
        "interval:  %llu.%03llu s (ref=%s)\n"
        "tsc:       %llu ns  drift=%lld ppm\n"
        "hpet:      %llu ns  drift=%lld ppm\n"
        "lapic1ms:  %llu ns  drift=%lld ppm\n",
        (unsigned long long)(ref / 1000000000ULL),
        (unsigned long long)((ref / 1000000ULL) % 1000ULL),
        refname,
        (unsigned long long)d_tsc,   (long long)drift_ppm(d_tsc, ref),
        (unsigned long long)d_hpet,  (long long)drift_ppm(d_hpet, ref),
        (unsigned long long)d_lapic, (long long)drift_ppm(d_lapic, ref));
}

static int gen_clocksource(char *buf, size_t max) {
    return clocksource_list(buf, (int)max);
}

static proc_file_t g_proc_files[] = {
    { "meminfo",     gen_meminfo,     {0} },
    { "cpuinfo",     gen_cpuinfo,     {0} },
    { "uptime",      gen_uptime,      {0} },
    { "loadavg",     gen_loadavg,     {0} },
    { "version",     gen_version,     {0} },
    { "mounts",      gen_mounts,      {0} },
    { "timedrift",   gen_timedrift,   {0} },
    { "clocksource", gen_clocksource, {0} },
    { "stat",        gen_stat,        {0} },
};
#define PROC_NFILES (sizeof(g_proc_files) / sizeof(g_proc_files[0]))

static vnode_t g_proc_root;
static uint64_t g_proc_ino = 1;

static int gen_net_tcp(char *buf, size_t max)  { return tcp_proc_list(buf, max, AF_INET); }
static int gen_net_tcp6(char *buf, size_t max) { return tcp_proc_list(buf, max, AF_INET6); }
static int gen_net_udp(char *buf, size_t max)  { return sock_proc_udp(buf, max, AF_INET); }
static int gen_net_udp6(char *buf, size_t max) { return sock_proc_udp(buf, max, AF_INET6); }

static int gen_net_snmp(char *buf, size_t max) {
    int n = tcp_proc_snmp(buf, max);
    if (n < 0 || (size_t)n >= max) return n;
    return n + sock_proc_udp_snmp(buf + n, max - (size_t)n);
}

static int gen_net_netstat(char *buf, size_t max) { return tcp_proc_netstat(buf, max); }
static int gen_net_dev(char *buf, size_t max)   { return netdev_proc_dev(buf, max); }
static int gen_net_route(char *buf, size_t max) { return netdev_proc_route(buf, max); }
static int gen_net_arp(char *buf, size_t max)   { return arp_proc_list(buf, max); }

static int gen_net_resolv(char *buf, size_t max) {
    int off = 0;
    for (int i = 0; ; i++) {
        netdev_t *d = netdev_at(i);
        if (!d) break;
        if (!d->dns) continue;
        uint32_t a = d->dns;
        off += snprintf(buf + off, max - (size_t)off, "nameserver %u.%u.%u.%u\n",
                        (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
        if ((size_t)off >= max) break;
    }
    return off;
}

static proc_file_t g_proc_net_files[] = {
    { "tcp",  gen_net_tcp,  {0} },
    { "tcp6", gen_net_tcp6, {0} },
    { "udp",  gen_net_udp,  {0} },
    { "udp6", gen_net_udp6, {0} },
    { "snmp", gen_net_snmp, {0} },
    { "netstat", gen_net_netstat, {0} },
    { "dev",     gen_net_dev,     {0} },
    { "route",   gen_net_route,   {0} },
    { "arp",     gen_net_arp,     {0} },
    { "resolv.conf", gen_net_resolv, {0} },
};
#define PROC_NET_NFILES (sizeof(g_proc_net_files) / sizeof(g_proc_net_files[0]))

static vnode_t g_proc_net_dir;

#define PROC_BIG_MAX 65536

static int64_t procfile_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    proc_file_t *pf = (proc_file_t *)node->fs_data;
    char *tmp = kmalloc(PROC_BIG_MAX);
    if (!tmp) return -ENOMEM;
    int total = pf->gen(tmp, PROC_BIG_MAX);
    if (total < 0) total = 0;
    if (total > PROC_BIG_MAX) total = PROC_BIG_MAX;
    int64_t r = 0;
    if (offset < (uint64_t)total) {
        size_t avail = (size_t)total - (size_t)offset;
        if (len > avail) len = avail;
        memcpy(buf, tmp + offset, len);
        r = (int64_t)len;
    }
    kfree(tmp);
    return r;
}

static int procfile_stat(vnode_t *node, vfs_stat_t *out) {
    proc_file_t *pf = (proc_file_t *)node->fs_data;
    char *tmp = kmalloc(PROC_BIG_MAX);
    if (!tmp) return -ENOMEM;
    int total = pf->gen(tmp, PROC_BIG_MAX);
    kfree(tmp);
    if (total < 0) total = 0;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_FILE;
    out->st_mode = 0444;
    out->st_size = (uint64_t)total;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
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

static const char *linux_state(task_t *t) {
    switch (t->state) {
        case TASK_RUNNING:
        case TASK_READY:   return "R (running)";
        case TASK_BLOCKED: return (t->pt_flags & PT_STOPPED) ? "t (tracing stop)" : "S (sleeping)";
        case TASK_ZOMBIE:  return "Z (zombie)";
        default:           return "X (dead)";
    }
}

static uint64_t task_rss_pages(task_t *t) {
    if (!t->is_userspace || !t->pagemap) return 0;
    struct mm *m = t->mm;
    if (!m || !mm_tryget(m)) return vmm_count_user_pages(t->pagemap);
    uint64_t n = mm_rss_pages(m);
    mm_put(m);
    return n;
}

static int gen_pid_status(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    uint64_t rss = (t->is_userspace && t->pagemap)
                 ? task_rss_pages(t) * 4096 : 0;
    uint64_t vsz = 0;
    struct mm *m = t->mm;
    if (m && mm_tryget(m)) { vsz = mm_vm_bytes(m); mm_put(m); }
    task_t *tr = t->ptracer;
    return snprintf(buf, max,
        "Name:\t%s\n"
        "Umask:\t%04o\n"
        "State:\t%s\n"
        "Tgid:\t%u\n"
        "Ngid:\t0\n"
        "Pid:\t%u\n"
        "PPid:\t%u\n"
        "TracerPid:\t%u\n"
        "Uid:\t%u\t%u\t%u\t%u\n"
        "Gid:\t%u\t%u\t%u\t%u\n"
        "FDSize:\t%d\n"
        "PGid:\t%u\n"
        "Sid:\t%u\n"
        "Prio:\t%d\n"
        "VmSize:\t%llu kB\n"
        "VmRSS:\t%llu kB\n"
        "Runtime:\t%llu ns\n"
        "Threads:\t%d\n"
        "SigPnd:\t%016llx\n"
        "SigBlk:\t%016llx\n",
        t->name, t->umask & 0777, linux_state(t), task_tgid(t), t->pid, task_leader(t)->ppid,
        tr ? task_tgid(tr) : 0,
        t->uid, t->uid, t->uid, t->uid, t->gid, t->gid, t->gid, t->gid,
        fd_table_capacity(t->fd_table), t->pgid, t->sid, t->priority,
        (unsigned long long)(vsz / 1024),
        (unsigned long long)(rss / 1024),
        (unsigned long long)t->total_runtime,
        task_leader(t)->group_live > 0 ? (int)task_leader(t)->group_live : 1,
        (unsigned long long)t->sig_pending, (unsigned long long)t->sig_blocked);
}

static int read_user_range(task_t *t, uintptr_t start, uintptr_t end, char *buf, size_t max) {
    struct mm *m = t->mm;
    if (!m || !start || end <= start || !mm_tryget(m)) return 0;
    size_t len = end - start;
    if (len > max) len = max;
    int n = mm_access_remote(m, start, buf, len, 0);
    mm_put(m);
    return n < 0 ? 0 : n;
}

static int gen_pid_mountinfo(uint32_t pid, char *buf, size_t max) {
    (void)pid;
    vfs_mount_info_t mi[VFS_MAX_MOUNTS];
    int n = vfs_list_mounts(mi, VFS_MAX_MOUNTS);
    int off = 0;
    for (int i = 0; i < n && (size_t)off < max; i++) {
        const char *dev = mi[i].device[0] ? mi[i].device : "none";
        int id = 20 + i;
        int parent = strcmp(mi[i].path, "/") == 0 ? id : 20;
        off += snprintf(buf + off, max - (size_t)off, "%d %d 0:%d / %s rw,relatime - %s %s rw\n",
                        id, parent, i + 1, mi[i].path, mi[i].fstype, dev);
    }
    return off;
}

static int gen_pid_cgroup(uint32_t pid, char *buf, size_t max) {
    (void)pid;
    return snprintf(buf, max, "0::/\n");
}

static int gen_pid_limits(uint32_t pid, char *buf, size_t max) {
    static const char *const names[RLIMIT_COUNT] = {
        "Max cpu time", "Max file size", "Max data size", "Max stack size", "Max core file size",
        "Max resident set", "Max processes", "Max open files", "Max locked memory", "Max address space",
        "Max file locks", "Max pending signals", "Max msgqueue size", "Max nice priority",
        "Max realtime priority", "Max realtime timeout",
    };
    static const char *const units[RLIMIT_COUNT] = {
        "seconds", "bytes", "bytes", "bytes", "bytes", "bytes", "processes", "files", "bytes", "bytes",
        "locks", "signals", "bytes", "", "", "us",
    };
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    int off = snprintf(buf, max, "%-25s %-20s %-20s %-10s\n", "Limit", "Soft Limit", "Hard Limit", "Units");
    for (int i = 0; i < RLIMIT_COUNT && (size_t)off < max; i++) {
        char sv[24], hv[24];
        if (t->rlim[i].cur == RLIM_INF_K) strcpy(sv, "unlimited");
        else snprintf(sv, sizeof sv, "%llu", (unsigned long long)t->rlim[i].cur);
        if (t->rlim[i].max == RLIM_INF_K) strcpy(hv, "unlimited");
        else snprintf(hv, sizeof hv, "%llu", (unsigned long long)t->rlim[i].max);
        off += snprintf(buf + off, max - (size_t)off, "%-25s %-20s %-20s %-10s\n", names[i], sv, hv, units[i]);
    }
    return off;
}

static int gen_pid_comm(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    return snprintf(buf, max, "%.15s\n", t->name);
}

static int gen_pid_environ(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    struct mm *m = t->mm;
    return m ? read_user_range(t, m->env_start, m->env_end, buf, max) : 0;
}

static int gen_pid_auxv(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    struct mm *m = t->mm;
    if (!m) return 0;
    size_t n = (size_t)m->nauxv * 8;
    if (n > max) n = max;
    memcpy(buf, m->auxv, n);
    return (int)n;
}

static int gen_pid_syscall(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    if (!t->in_syscall) return snprintf(buf, max, "running\n");
    return snprintf(buf, max, "%llu 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n",
        (unsigned long long)t->cur_syscall,
        (unsigned long long)t->cur_syscall_args[0], (unsigned long long)t->cur_syscall_args[1],
        (unsigned long long)t->cur_syscall_args[2], (unsigned long long)t->cur_syscall_args[3],
        (unsigned long long)t->cur_syscall_args[4], (unsigned long long)t->cur_syscall_args[5],
        (unsigned long long)t->user_rsp, (unsigned long long)t->user_saved_rip);
}

extern const char *ksym_lookup(uint64_t addr, uint64_t *off_out);

static int gen_pid_stack(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t || !t->stack_base) return -1;
    if (t->state == TASK_RUNNING) return snprintf(buf, max, "running\n");
    uint64_t lo = t->stack_base, hi = t->stack_base + KERNEL_STACK_PAGES * 4096ULL;
    uint64_t sp = t->rsp;
    if (sp < lo || sp + 7 * 8 > hi) return snprintf(buf, max, "no saved frame\n");
    const uint64_t *f = (const uint64_t *)sp;
    uint64_t ret = f[6];
    uint64_t rbp = f[5];
    int n = 0;
    for (int depth = 0; depth < 24 && (size_t)n < max; depth++) {
        uint64_t off = 0;
        const char *name = ksym_lookup(ret, &off);
        n += snprintf(buf + n, max - (size_t)n, "[<%016llx>] %s+0x%llx\n",
                      (unsigned long long)ret, name ? name : "?", (unsigned long long)off);
        if (rbp < lo || rbp + 16 > hi || (rbp & 7)) break;
        const uint64_t *fr = (const uint64_t *)rbp;
        ret = fr[1];
        uint64_t next = fr[0];
        if (next <= rbp) break;
        rbp = next;
    }
    return n;
}

static int gen_pid_cmdline(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    struct mm *m = t->mm;
    int n = m ? read_user_range(t, m->arg_start, m->arg_end, buf, max) : 0;
    if (n > 0) return n;
    return snprintf(buf, max, "%s", t->name) + 1;
}

static int gen_pid_stat(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    struct mm *m = t->mm;
    uint64_t vsz = 0, rss = 0;
    if (m && mm_tryget(m)) { vsz = mm_vm_bytes(m); mm_put(m); }
    rss = task_rss_pages(t);
    uint64_t ticks = t->total_runtime / 10000000ULL;
    uint64_t start = t->create_time_ns / 10000000ULL;
    int threads = task_leader(t)->group_live > 0 ? (int)task_leader(t)->group_live : 1;
    return snprintf(buf, max,
        "%u (%s) %c %u %u %u 0 -1 0 0 0 0 0 %llu 0 0 0 %d %d %d 0 %llu %llu %llu "
        "18446744073709551615 0 0 0 0 0 %llu %llu 0 0 0 0 0 17 %u 0 0 0 0 0 0 0 0 "
        "%llu %llu %llu %llu %d\n",
        t->pid, t->name, linux_state(t)[0], task_leader(t)->ppid, t->pgid, t->sid,
        (unsigned long long)ticks, t->priority, t->nice, threads,
        (unsigned long long)start, (unsigned long long)vsz, (unsigned long long)rss,
        (unsigned long long)t->sig_pending, (unsigned long long)t->sig_blocked, t->cpu_id,
        (unsigned long long)(m ? m->arg_start : 0), (unsigned long long)(m ? m->arg_end : 0),
        (unsigned long long)(m ? m->env_start : 0), (unsigned long long)(m ? m->env_end : 0),
        t->exit_code);
}

static const char *vnode_type_name(int type) {
    switch (type) {
        case VFS_NODE_FILE:    return "file";
        case VFS_NODE_DIR:     return "dir";
        case VFS_NODE_CHARDEV: return "chardev";
        case VFS_NODE_SOCKET:  return "socket";
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
    int fdcap = fd_table_capacity(t->fd_table);
    for (int fd = 0; fd < fdcap && off < max; fd++) {
        int type = -1, oflags = 0;
        if (vfs_fd_info(t->fd_table, fd, &type, &oflags) < 0) continue;
        const char *acc = ((oflags & 3) == 0) ? "r" : ((oflags & 3) == 1) ? "w" : "rw";
        char what[160] = "";
        int refs = 0;
        void *vn = NULL;
        vfs_file_t *file = fd_get(t->fd_table, fd);
        if (file) {
            refs = file->refcount - 1;
            vn = file->vnode;
            if (file->vnode) unix_describe(file->vnode, what, sizeof what);
            fd_put(file);
        }
        int cloexec = (fd_get_flags(t->fd_table, fd) & FD_CLOEXEC) != 0;
        int w = snprintf(buf + off, max - off, "%d %s %s vnode=%p refs=%d%s %s\n",
                         fd, vnode_type_name(type), acc, vn, refs,
                         cloexec ? " cloexec" : "", what);
        if (w < 0) break;
        off += (size_t)w;
    }
    return (int)off;
}

static int gen_pid_puzzle(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    if (!t->puzzle)
        return snprintf(buf, max, "no puzzle (not an execve'd program)\n");
    return puzzle_format(t->puzzle, buf, max);
}

static int gen_pid_maps(uint32_t pid, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t) return -1;
    struct mm *m = t->mm;
    if (!m || !mm_tryget(m)) return 0;
    int n = mm_format_maps(m, buf, max);
    mm_put(m);
    return n;
}

typedef int (*proc_pid_gen_fn)(uint32_t pid, char *buf, size_t max);

typedef struct { const char *name; proc_pid_gen_fn gen; } proc_pid_file_t;

static const proc_pid_file_t g_pid_files[] = {
    { "status",  gen_pid_status  },
    { "cmdline", gen_pid_cmdline },
    { "stat",    gen_pid_stat    },
    { "fds",     gen_pid_fd      },
    { "syscall", gen_pid_syscall },
    { "stack",   gen_pid_stack   },
    { "puzzle",  gen_pid_puzzle  },
    { "maps",    gen_pid_maps    },
    { "environ", gen_pid_environ },
    { "auxv",    gen_pid_auxv    },
    { "comm",    gen_pid_comm    },
    { "mountinfo", gen_pid_mountinfo },
    { "cgroup",  gen_pid_cgroup  },
    { "limits",  gen_pid_limits  },
};
#define PROC_PID_NFILES (sizeof(g_pid_files) / sizeof(g_pid_files[0]))

typedef struct {
    uint32_t        pid;
    proc_pid_gen_fn gen;
} proc_pid_data_t;

#define PROC_PID_MAX (4u << 20)

static char *pidfile_generate(proc_pid_data_t *pd, int *total) {
    size_t cap = PROC_BUF_MAX;
    for (;;) {
        char *tmp = kmalloc(cap);
        if (!tmp) { *total = -ENOMEM; return NULL; }
        int n = pd->gen(pd->pid, tmp, cap);
        if (n < 0) { kfree(tmp); *total = n; return NULL; }
        if ((size_t)n + 1 < cap || cap >= PROC_PID_MAX) {
            if ((size_t)n >= cap) n = (int)strnlen(tmp, cap);
            *total = n;
            return tmp;
        }
        kfree(tmp);
        cap *= 2;
    }
}

static int64_t pidfile_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    proc_pid_data_t *pd = (proc_pid_data_t *)node->fs_data;
    int total;
    char *tmp = pidfile_generate(pd, &total);
    if (!tmp) return total == -ENOMEM ? -ENOMEM : -ESRCH;
    int64_t r = 0;
    if (offset < (uint64_t)total) {
        size_t avail = (size_t)total - (size_t)offset;
        if (len > avail) len = avail;
        memcpy(buf, tmp + offset, len);
        r = (int64_t)len;
    }
    kfree(tmp);
    return r;
}

static int pidfile_stat(vnode_t *node, vfs_stat_t *out) {
    proc_pid_data_t *pd = (proc_pid_data_t *)node->fs_data;
    int total;
    char *tmp = pidfile_generate(pd, &total);
    if (tmp) kfree(tmp);
    if (total < 0) total = 0;
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_FILE;
    out->st_mode = 0444;
    out->st_size = (uint64_t)total;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
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

static int64_t pidexe_readlink(vnode_t *node, char *buf, size_t bufsiz) {
    task_t *t = task_find_by_pid((uint32_t)(uintptr_t)node->fs_data);
    if (!t || !t->exe[0]) return -ENOENT;
    size_t n = strlen(t->exe);
    if (n > bufsiz) n = bufsiz;
    memcpy(buf, t->exe, n);
    return (int64_t)n;
}

static int pidexe_stat(vnode_t *node, vfs_stat_t *out) {
    task_t *t = task_find_by_pid((uint32_t)(uintptr_t)node->fs_data);
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_SYMLINK;
    out->st_mode = 0777;
    out->st_size = t ? strlen(t->exe) : 0;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
    return 0;
}

static void pidexe_unref(vnode_t *node) {
    kfree(node);
}

static const vnode_ops_t pidexe_ops = {
    .readlink = pidexe_readlink,
    .stat     = pidexe_stat,
    .ref      = proc_ref,
    .unref    = pidexe_unref,
};

typedef struct {
    uint32_t pid;
    int      fd;
} proc_fd_link_t;

static int fd_link_target(uint32_t pid, int fd, char *buf, size_t max) {
    task_t *t = task_find_by_pid(pid);
    if (!t || !t->fd_table || fd < 0 || fd >= TASK_MAX_FDS) return -ENOENT;
    vfs_file_t *f = fd_get(t->fd_table, fd);
    if (!f) return -ENOENT;
    vnode_t *v = f->vnode;
    int n;
    if (f->path && f->path[0] == '/')
        n = snprintf(buf, max, "%s", f->path);
    else if (v && v->type == VFS_NODE_PIPE)
        n = snprintf(buf, max, "pipe:[%llu]", (unsigned long long)v->ino);
    else if (v && v->type == VFS_NODE_SOCKET)
        n = snprintf(buf, max, "socket:[%llu]", (unsigned long long)v->ino);
    else
        n = snprintf(buf, max, "anon_inode:[%s]", v ? vnode_type_name((int)v->type) : "?");
    fd_put(f);
    if (n < 0) return -EIO;
    return (size_t)n < max ? n : (int)max - 1;
}

static int64_t fdlink_readlink(vnode_t *node, char *buf, size_t bufsiz) {
    proc_fd_link_t *l = node->fs_data;
    char tmp[VFS_MAX_PATH];
    int n = fd_link_target(l->pid, l->fd, tmp, sizeof tmp);
    if (n < 0) return n;
    if ((size_t)n > bufsiz) n = (int)bufsiz;
    memcpy(buf, tmp, (size_t)n);
    return n;
}

static int fdlink_stat(vnode_t *node, vfs_stat_t *out) {
    proc_fd_link_t *l = node->fs_data;
    char tmp[VFS_MAX_PATH];
    int n = fd_link_target(l->pid, l->fd, tmp, sizeof tmp);
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_SYMLINK;
    out->st_mode = 0700;
    out->st_size = n > 0 ? (uint64_t)n : 0;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
    return 0;
}

static const vnode_ops_t fdlink_ops = {
    .readlink = fdlink_readlink,
    .stat     = fdlink_stat,
    .ref      = proc_ref,
    .unref    = piddyn_unref,
};

static int parse_fd(const char *s, int *out) {
    if (!s[0] || (s[0] == '0' && s[1])) return -1;
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9' || v > TASK_MAX_FDS) return -1;
        v = v * 10 + (*p - '0');
    }
    *out = v;
    return 0;
}

static int fddir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    uint32_t pid = (uint32_t)(uintptr_t)dir->fs_data;
    int fd;
    char tmp[8];
    if (parse_fd(name, &fd) < 0 || fd_link_target(pid, fd, tmp, sizeof tmp) < 0) return -ENOENT;
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return -ENOMEM;
    proc_fd_link_t *l = kzalloc(sizeof(proc_fd_link_t));
    if (!l) { kfree(v); return -ENOMEM; }
    l->pid = pid;
    l->fd  = fd;
    v->type = VFS_NODE_SYMLINK;
    v->mode = 0700;
    v->ino  = g_proc_ino++;
    v->ops  = &fdlink_ops;
    v->fs_data = l;
    v->refcount = 1;
    *out = v;
    return 0;
}

static int fddir_readdir(vnode_t *dir, uint64_t *pos, vfs_dirent_t *out) {
    uint32_t pid = (uint32_t)(uintptr_t)dir->fs_data;
    task_t *t = task_find_by_pid(pid);
    if (!t || !t->fd_table) return -ENOENT;
    uint64_t fdcap = (uint64_t)fd_table_capacity(t->fd_table);
    for (uint64_t fd = *pos; fd < fdcap; fd++) {
        int type, oflags;
        if (vfs_fd_info(t->fd_table, (int)fd, &type, &oflags) < 0) continue;
        out->d_ino  = fd + 1;
        out->d_type = (uint8_t)VFS_NODE_SYMLINK;
        snprintf(out->d_name, VFS_MAX_NAME, "%d", (int)fd);
        *pos = fd + 1;
        return 0;
    }
    *pos = fdcap;
    return -ENOENT;
}

static int fddir_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_DIR;
    out->st_mode = 0500;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
    return 0;
}

static void fddir_unref(vnode_t *node) {
    kfree(node);
}

static const vnode_ops_t fddir_ops = {
    .lookup  = fddir_lookup,
    .readdir = fddir_readdir,
    .stat    = fddir_stat,
    .ref     = proc_ref,
    .unref   = fddir_unref,
};

static bool mem_allowed(task_t *t) {
    task_t *me = (task_t *)percpu_current_task();
    if (!me || !t) return false;
    if (me->uid == 0 || me->uid == t->uid) return true;
    return t->ptracer && task_leader(t->ptracer) == task_leader(me);
}

static int64_t pidmem_rw(vnode_t *node, void *buf, size_t len, uint64_t offset, bool write) {
    task_t *t = task_find_by_pid((uint32_t)(uintptr_t)node->fs_data);
    if (!t) return -ESRCH;
    if (!mem_allowed(t)) return -EACCES;
    if (offset >= 0x0000800000000000ULL) return -EIO;
    if (len > 0x0000800000000000ULL - offset) len = 0x0000800000000000ULL - offset;
    int n = ptrace_access(t, (uintptr_t)offset, buf, len, write);
    return n < 0 ? -EIO : n;
}

static int64_t pidmem_read(vnode_t *node, void *buf, size_t len, uint64_t offset) {
    return pidmem_rw(node, buf, len, offset, false);
}

static int64_t pidmem_write(vnode_t *node, const void *buf, size_t len, uint64_t offset) {
    return pidmem_rw(node, (void *)buf, len, offset, true);
}

static int pidmem_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_FILE;
    out->st_mode = 0600;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
    return 0;
}

static const vnode_ops_t pidmem_ops = {
    .read  = pidmem_read,
    .write = pidmem_write,
    .stat  = pidmem_stat,
    .ref   = proc_ref,
    .unref = pidexe_unref,
};

static int make_piddir(uint32_t pid, vnode_t **out);

static int taskdir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    task_t *leader = task_find_by_pid((uint32_t)(uintptr_t)dir->fs_data);
    uint32_t tid = 0;
    for (const char *p = name; *p; p++) {
        if (*p < '0' || *p > '9') return -ENOENT;
        tid = tid * 10 + (uint32_t)(*p - '0');
    }
    task_t *t = task_find_by_pid(tid);
    if (!leader || !t || task_leader(t) != task_leader(leader)) return -ENOENT;
    return make_piddir(tid, out);
}

static int taskdir_readdir(vnode_t *dir, uint64_t *pos, vfs_dirent_t *out) {
    task_t *leader = task_find_by_pid((uint32_t)(uintptr_t)dir->fs_data);
    if (!leader) return -ENOENT;
    uint32_t all[256];
    int n = task_collect_pids(all, 256);
    uint64_t k = 0;
    for (int i = 0; i < n; i++) {
        task_t *t = task_find_by_pid(all[i]);
        if (!t || task_leader(t) != task_leader(leader)) continue;
        if (t->state == TASK_ZOMBIE || t->state == TASK_DEAD) continue;
        if (k++ < *pos) continue;
        out->d_ino  = all[i];
        out->d_type = (uint8_t)VFS_NODE_DIR;
        snprintf(out->d_name, VFS_MAX_NAME, "%u", all[i]);
        (*pos)++;
        return 0;
    }
    return -ENOENT;
}

static const vnode_ops_t taskdir_ops = {
    .lookup  = taskdir_lookup,
    .readdir = taskdir_readdir,
    .stat    = fddir_stat,
    .ref     = proc_ref,
    .unref   = fddir_unref,
};

static int simple_node(uint32_t pid, int type, uint32_t mode, const vnode_ops_t *ops, vnode_t **out) {
    vnode_t *v = kzalloc(sizeof(vnode_t));
    if (!v) return -ENOMEM;
    v->type = type;
    v->mode = mode;
    v->ino  = g_proc_ino++;
    v->ops  = ops;
    v->fs_data = (void *)(uintptr_t)pid;
    v->refcount = 1;
    *out = v;
    return 0;
}

static int piddir_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    uint32_t pid = (uint32_t)(uintptr_t)dir->fs_data;
    if (strcmp(name, "fd") == 0) {
        vnode_t *v = kzalloc(sizeof(vnode_t));
        if (!v) return -ENOMEM;
        v->type = VFS_NODE_DIR;
        v->mode = 0500;
        v->ino  = g_proc_ino++;
        v->ops  = &fddir_ops;
        v->fs_data = (void *)(uintptr_t)pid;
        v->refcount = 1;
        *out = v;
        return 0;
    }
    if (strcmp(name, "mem") == 0) return simple_node(pid, VFS_NODE_FILE, 0600, &pidmem_ops, out);
    if (strcmp(name, "task") == 0) return simple_node(pid, VFS_NODE_DIR, 0555, &taskdir_ops, out);
    if (strcmp(name, "exe") == 0) {
        vnode_t *v = kzalloc(sizeof(vnode_t));
        if (!v) return -ENOMEM;
        v->type = VFS_NODE_SYMLINK;
        v->mode = 0777;
        v->ino  = g_proc_ino++;
        v->ops  = &pidexe_ops;
        v->fs_data = (void *)(uintptr_t)pid;
        v->refcount = 1;
        *out = v;
        return 0;
    }
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

static int piddir_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out);

static int piddir_readdir(vnode_t *dir, uint64_t *pos, vfs_dirent_t *out) {
    int r = piddir_readdir_at(dir, *pos, out);
    if (r == 0) (*pos)++;
    return r;
}

static int piddir_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (index == PROC_PID_NFILES) {
        out->d_ino  = index + 1;
        out->d_type = (uint8_t)VFS_NODE_SYMLINK;
        strcpy(out->d_name, "exe");
        return 0;
    }
    if (index == PROC_PID_NFILES + 1) {
        out->d_ino  = index + 1;
        out->d_type = (uint8_t)VFS_NODE_DIR;
        strcpy(out->d_name, "fd");
        return 0;
    }
    if (index == PROC_PID_NFILES + 2) {
        out->d_ino  = index + 1;
        out->d_type = (uint8_t)VFS_NODE_FILE;
        strcpy(out->d_name, "mem");
        return 0;
    }
    if (index == PROC_PID_NFILES + 3) {
        out->d_ino  = index + 1;
        out->d_type = (uint8_t)VFS_NODE_DIR;
        strcpy(out->d_name, "task");
        return 0;
    }
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
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
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

static int procnet_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    (void)dir;
    for (size_t i = 0; i < PROC_NET_NFILES; i++) {
        if (strcmp(g_proc_net_files[i].name, name) == 0) {
            vnode_ref(&g_proc_net_files[i].vnode);
            *out = &g_proc_net_files[i].vnode;
            return 0;
        }
    }
    return -ENOENT;
}

static int procnet_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out);

static int procnet_readdir(vnode_t *dir, uint64_t *pos, vfs_dirent_t *out) {
    int r = procnet_readdir_at(dir, *pos, out);
    if (r == 0) (*pos)++;
    return r;
}

static int procnet_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (index >= PROC_NET_NFILES) return -ENOENT;
    out->d_ino  = g_proc_net_files[index].vnode.ino;
    out->d_type = (uint8_t)VFS_NODE_FILE;
    strncpy(out->d_name, g_proc_net_files[index].name, VFS_MAX_NAME - 1);
    out->d_name[VFS_MAX_NAME - 1] = '\0';
    return 0;
}

static int procnet_stat(vnode_t *node, vfs_stat_t *out) {
    memset(out, 0, sizeof(*out));
    out->st_ino  = node->ino;
    out->st_type = VFS_NODE_DIR;
    out->st_mode = 0555;
    out->st_size = PROC_NET_NFILES;
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
    return 0;
}

static const vnode_ops_t procnet_ops = {
    .lookup  = procnet_lookup,
    .readdir = procnet_readdir,
    .stat    = procnet_stat,
    .ref     = proc_ref,
    .unref   = proc_unref,
};

static int procroot_lookup(vnode_t *dir, const char *name, vnode_t **out) {
    (void)dir;
    if (strcmp(name, "net") == 0) {
        vnode_ref(&g_proc_net_dir);
        *out = &g_proc_net_dir;
        return 0;
    }
    for (size_t i = 0; i < PROC_NFILES; i++) {
        if (strcmp(g_proc_files[i].name, name) == 0) {
            vnode_ref(&g_proc_files[i].vnode);
            *out = &g_proc_files[i].vnode;
            return 0;
        }
    }
    if (strcmp(name, "self") == 0) {
        task_t *me = (task_t *)percpu_current_task();
        if (me) return make_piddir(me->pid, out);
        return -ENOENT;
    }
    uint32_t pid;
    if (parse_pid(name, &pid) == 0)
        return make_piddir(pid, out);
    return -ENOENT;
}

static int procroot_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out);

static int procroot_readdir(vnode_t *dir, uint64_t *pos, vfs_dirent_t *out) {
    int r = procroot_readdir_at(dir, *pos, out);
    if (r == 0) (*pos)++;
    return r;
}

static int procroot_readdir_at(vnode_t *dir, uint64_t index, vfs_dirent_t *out) {
    (void)dir;
    if (index < PROC_NFILES) {
        out->d_ino  = g_proc_files[index].vnode.ino;
        out->d_type = (uint8_t)VFS_NODE_FILE;
        strncpy(out->d_name, g_proc_files[index].name, VFS_MAX_NAME - 1);
        out->d_name[VFS_MAX_NAME - 1] = '\0';
        return 0;
    }
    if (index == PROC_NFILES) {
        out->d_ino  = g_proc_net_dir.ino;
        out->d_type = (uint8_t)VFS_NODE_DIR;
        strncpy(out->d_name, "net", VFS_MAX_NAME - 1);
        return 0;
    }
    uint32_t all[256], pids[256];
    int nall = task_collect_pids(all, 256), npids = 0;
    for (int i = 0; i < nall; i++) {
        task_t *t = task_find_by_pid(all[i]);
        if (t && task_leader(t) == t) pids[npids++] = all[i];
    }
    uint64_t pidx = index - PROC_NFILES - 1;
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
    out->st_atime = out->st_mtime = out->st_ctime = vfs_boot_time();
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

    memset(&g_proc_net_dir, 0, sizeof(g_proc_net_dir));
    g_proc_net_dir.type     = VFS_NODE_DIR;
    g_proc_net_dir.mode     = 0555;
    g_proc_net_dir.ino      = g_proc_ino++;
    g_proc_net_dir.ops      = &procnet_ops;
    g_proc_net_dir.refcount = 1;
    for (size_t i = 0; i < PROC_NET_NFILES; i++) {
        vnode_t *v = &g_proc_net_files[i].vnode;
        memset(v, 0, sizeof(*v));
        v->type     = VFS_NODE_FILE;
        v->mode     = 0444;
        v->ino      = g_proc_ino++;
        v->ops      = &procfile_ops;
        v->fs_data  = &g_proc_net_files[i];
        v->refcount = 1;
    }

    serial_writestring("[procfs] /proc mounted (meminfo,cpuinfo,uptime,version,mounts,<pid>/,self)\n");
    return &g_proc_root;
}
