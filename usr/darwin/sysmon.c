#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include "darwin.h"

#define KINFO_SIZE 648
#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_INVALID_ARGUMENT 4

struct pinfo {
    int pid, ppid, pgid, sid, uid, gid, threads, prio, nice;
    char state;
    char comm[17];
    uint64_t vsize, rss, runtime_ns, start_sec, start_usec;
};

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = 0;
    return (int)n;
}

static long status_field(const char *st, const char *key)
{
    const char *p = strstr(st, key);
    if (!p)
        return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t')
        p++;
    return strtol(p, NULL, 10);
}

static uint64_t boot_time(void)
{
    static uint64_t bt;
    if (!bt) {
        char buf[4096];
        if (read_text("/proc/stat", buf, sizeof buf) > 0) {
            const char *p = strstr(buf, "btime ");
            if (p)
                bt = strtoull(p + 6, NULL, 10);
        }
    }
    return bt;
}

static uint64_t group_runtime(int pid, uint64_t own)
{
    char d[64];
    snprintf(d, sizeof d, "/proc/%d/task", pid);
    DIR *dir = opendir(d);
    if (!dir)
        return own;
    uint64_t sum = 0;
    int any = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        char p[96], st[2048];
        snprintf(p, sizeof p, "/proc/%s/status", e->d_name);
        if (read_text(p, st, sizeof st) > 0) {
            sum += (uint64_t)status_field(st, "Runtime:");
            any = 1;
        }
    }
    closedir(dir);
    return any ? sum : own;
}

static int read_pinfo(int pid, struct pinfo *pi)
{
    char path[64], st[2048], sb[1024];
    memset(pi, 0, sizeof *pi);
    snprintf(path, sizeof path, "/proc/%d/status", pid);
    if (read_text(path, st, sizeof st) <= 0)
        return -1;
    if (status_field(st, "Tgid:") != pid)
        return -1;
    pi->pid = pid;
    pi->ppid = (int)status_field(st, "PPid:");
    pi->pgid = (int)status_field(st, "PGid:");
    pi->sid = (int)status_field(st, "Sid:");
    pi->uid = (int)status_field(st, "Uid:");
    pi->gid = (int)status_field(st, "Gid:");
    pi->threads = (int)status_field(st, "Threads:");
    pi->prio = (int)status_field(st, "Prio:");
    pi->vsize = (uint64_t)status_field(st, "VmSize:") * 1024;
    pi->rss = (uint64_t)status_field(st, "VmRSS:") * 1024;
    pi->runtime_ns = group_runtime(pid, (uint64_t)status_field(st, "Runtime:"));
    const char *s = strstr(st, "State:");
    pi->state = 'S';
    if (s) {
        s += 6;
        while (*s == ' ' || *s == '\t')
            s++;
        pi->state = *s;
    }
    const char *nm = strstr(st, "Name:");
    if (nm) {
        nm += 5;
        while (*nm == ' ' || *nm == '\t')
            nm++;
        size_t l = strcspn(nm, "\n");
        if (l > 16)
            l = 16;
        memcpy(pi->comm, nm, l);
    }
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    if (read_text(path, sb, sizeof sb) > 0) {
        const char *r = strrchr(sb, ')');
        if (r) {
            unsigned long long v[24] = { 0 };
            int i = 0;
            for (const char *p = r + 2; *p && i < 24; i++) {
                v[i] = strtoull(p, NULL, 10);
                p = strchr(p, ' ');
                if (!p)
                    break;
                p++;
            }
            pi->nice = (int)(long long)v[16];
            uint64_t ticks = v[19];
            pi->start_sec = boot_time() + ticks / 100;
            pi->start_usec = (ticks % 100) * 10000;
        }
    }
    if (pi->threads < 1)
        pi->threads = 1;
    return 0;
}

static int list_pids(int *out, int cap)
{
    DIR *d = opendir("/proc");
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        int pid = atoi(e->d_name);
        char path[64], st[512];
        snprintf(path, sizeof path, "/proc/%d/status", pid);
        if (read_text(path, st, sizeof st) > 0 && status_field(st, "Tgid:") == pid)
            out[n++] = pid;
    }
    closedir(d);
    return n;
}

static int dstate(char s)
{
    switch (s) {
    case 'R': return 2;
    case 'T': case 't': return 4;
    case 'Z': case 'X': return 5;
    default: return 3;
    }
}

static void put32(uint8_t *b, size_t off, uint32_t v) { memcpy(b + off, &v, 4); }
static void put64(uint8_t *b, size_t off, uint64_t v) { memcpy(b + off, &v, 8); }

static void fill_kinfo(uint8_t *k, const struct pinfo *pi)
{
    memset(k, 0, KINFO_SIZE);
    put64(k, 0, pi->start_sec);
    put32(k, 8, (uint32_t)pi->start_usec);
    put32(k, 32, 0x4);
    k[36] = (uint8_t)dstate(pi->state);
    put32(k, 40, (uint32_t)pi->pid);
    k[240] = (uint8_t)pi->prio;
    k[241] = (uint8_t)pi->prio;
    k[242] = (uint8_t)(int8_t)pi->nice;
    memcpy(k + 243, pi->comm, 16);
    put32(k, 296 + 16 + 80, (uint32_t)pi->uid);
    put32(k, 296 + 16 + 84, (uint32_t)pi->uid);
    put32(k, 296 + 16 + 88, (uint32_t)pi->gid);
    put32(k, 296 + 16 + 92, (uint32_t)pi->gid);
    put32(k, 296 + 120, 1);
    put32(k, 296 + 124, (uint32_t)pi->uid);
    k[296 + 128] = 1;
    put32(k, 296 + 132, (uint32_t)pi->gid);
    put32(k, 296 + 264, (uint32_t)pi->ppid);
    put32(k, 296 + 268, (uint32_t)pi->pgid);
    put32(k, 296 + 276, 0xffffffffu);
    put32(k, 296 + 280, 0);
}

static int copy_out(const void *src, size_t n, void *out, size_t *len)
{
    if (out) {
        if (*len < n) {
            memcpy(out, src, *len);
            *len = n;
            errno = ENOMEM;
            return -1;
        }
        memcpy(out, src, n);
    }
    *len = n;
    return 0;
}

static int procargs(int pid, int with_argc, void *out, size_t *len)
{
    char path[64], exe[1024];
    static char args[65536];
    snprintf(path, sizeof path, "/proc/%d/cmdline", pid);
    int n = read_text(path, args, sizeof args);
    if (n < 0) {
        errno = ESRCH;
        return -1;
    }
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    ssize_t el = readlink(path, exe, sizeof exe - 1);
    if (el < 0)
        el = 0;
    exe[el] = 0;
    int argc = 0;
    for (int i = 0; i < n; i++)
        if (!args[i])
            argc++;
    size_t total = (with_argc ? 4 : 0) + (size_t)el + 1 + 8 + (size_t)n + 1;
    uint8_t *b = calloc(1, total);
    if (!b)
        return -1;
    size_t o = 0;
    if (with_argc) {
        memcpy(b, &argc, 4);
        o = 4;
    }
    memcpy(b + o, exe, (size_t)el);
    o += (size_t)el + 1 + 8;
    memcpy(b + o, args, (size_t)n);
    int r = copy_out(b, total, out, len);
    free(b);
    return r;
}

int d_sysctl_proc(const int *mib, unsigned n, void *out, size_t *len)
{
    if (mib[1] == 49 || mib[1] == 38) {
        if (n < 3) {
            errno = EINVAL;
            return -1;
        }
        return procargs(mib[2], mib[1] == 49, out, len);
    }
    int what = n >= 3 ? mib[2] : 0, arg = n >= 4 ? mib[3] : 0;
    static int pids[4096];
    int np = list_pids(pids, 4096);
    uint8_t *buf = malloc((size_t)(np ? np : 1) * KINFO_SIZE);
    if (!buf)
        return -1;
    int cnt = 0;
    for (int i = 0; i < np; i++) {
        struct pinfo pi;
        if (read_pinfo(pids[i], &pi) < 0)
            continue;
        int keep = what == 0 || (what == 1 && pi.pid == arg) || (what == 2 && pi.pgid == arg) ||
                   (what == 3 && pi.sid == arg) || ((what == 5 || what == 6) && pi.uid == arg);
        if (!keep)
            continue;
        fill_kinfo(buf + (size_t)cnt * KINFO_SIZE, &pi);
        cnt++;
    }
    if (what == 1 && !cnt) {
        free(buf);
        *len = 0;
        return 0;
    }
    size_t need = (size_t)cnt * KINFO_SIZE;
    int r;
    if (!out) {
        *len = need + 16 * KINFO_SIZE;
        r = 0;
    } else {
        size_t room = *len / KINFO_SIZE * KINFO_SIZE;
        if (room < need) {
            memcpy(out, buf, room);
            *len = room;
            errno = ENOMEM;
            r = -1;
        } else {
            memcpy(out, buf, need);
            *len = need;
            r = 0;
        }
    }
    free(buf);
    return r;
}

struct ifcount {
    char name[16];
    uint64_t rxb, rxp, txb, txp, rxd, txd;
};

static int read_ifcounts(struct ifcount *ic, int cap)
{
    char buf[8192];
    if (read_text("/proc/net/dev", buf, sizeof buf) <= 0)
        return 0;
    int n = 0;
    char *line = strchr(buf, '\n');
    if (line)
        line = strchr(line + 1, '\n');
    while (line && *++line && n < cap) {
        char *colon = strchr(line, ':');
        if (!colon)
            break;
        char *nm = line;
        while (*nm == ' ')
            nm++;
        size_t l = (size_t)(colon - nm);
        if (l >= sizeof ic[n].name)
            l = sizeof ic[n].name - 1;
        memcpy(ic[n].name, nm, l);
        ic[n].name[l] = 0;
        unsigned long long v[16] = { 0 };
        char *p = colon + 1;
        for (int i = 0; i < 16; i++) {
            v[i] = strtoull(p, &p, 10);
        }
        ic[n].rxb = v[0];
        ic[n].rxp = v[1];
        ic[n].rxd = v[3];
        ic[n].txb = v[8];
        ic[n].txp = v[9];
        ic[n].txd = v[11];
        n++;
        line = strchr(line, '\n');
    }
    return n;
}

int d_sysctl_route(const int *mib, unsigned n, void *out, size_t *len)
{
    if (n < 6 || (mib[4] != 6 && mib[4] != 3)) {
        errno = EINVAL;
        return -1;
    }
    struct ifcount ic[32];
    int ni = read_ifcounts(ic, 32);
    size_t rec = 160 + 20, total = (size_t)ni * rec;
    uint8_t *b = calloc(1, total ? total : 1);
    if (!b)
        return -1;
    for (int i = 0; i < ni; i++) {
        uint8_t *m = b + (size_t)i * rec;
        uint16_t msglen = 160;
        memcpy(m, &msglen, 2);
        m[2] = 5;
        m[3] = mib[4] == 6 ? 0x12 : 0x0e;
        put32(m, 4, 0x10);
        put32(m, 8, 0x1 | 0x40 | (strcmp(ic[i].name, "lo") ? 0x8000 : 0x8));
        uint16_t idx = (uint16_t)(i + 1);
        memcpy(m + 12, &idx, 2);
        uint8_t *d = m + 32;
        d[0] = strcmp(ic[i].name, "lo") ? 6 : 24;
        put32(d, 8, 1500);
        put64(d, 16, 1000000000ull);
        put64(d, 24, ic[i].rxp);
        put64(d, 40, ic[i].txp);
        put64(d, 64, ic[i].rxb);
        put64(d, 72, ic[i].txb);
        put64(d, 96, ic[i].rxd);
        uint8_t *sdl = m + 160;
        sdl[0] = 20;
        sdl[1] = 18;
        memcpy(sdl + 2, &idx, 2);
        size_t nl = strlen(ic[i].name);
        if (nl > 12)
            nl = 12;
        sdl[5] = (uint8_t)nl;
        memcpy(sdl + 8, ic[i].name, nl);
        msglen = (uint16_t)rec;
        memcpy(m, &msglen, 2);
    }
    int r = copy_out(b, total, out, len);
    free(b);
    return r;
}

static int ncpu(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static int cpu_ticks(uint32_t *per, int cap, uint32_t tot[4])
{
    char buf[8192];
    memset(tot, 0, 4 * sizeof *tot);
    if (read_text("/proc/stat", buf, sizeof buf) <= 0)
        return 0;
    int n = 0;
    for (char *l = buf; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
        if (strncmp(l, "cpu", 3))
            continue;
        unsigned long long u = 0, ni = 0, s = 0, id = 0;
        char *p = l + 3;
        int is_total = *p == ' ';
        if (!is_total)
            strtol(p, &p, 10);
        u = strtoull(p, &p, 10);
        ni = strtoull(p, &p, 10);
        s = strtoull(p, &p, 10);
        id = strtoull(p, &p, 10);
        if (is_total) {
            tot[0] = (uint32_t)u;
            tot[1] = (uint32_t)s;
            tot[2] = (uint32_t)id;
            tot[3] = (uint32_t)ni;
        } else if (n < cap) {
            per[n * 4 + 0] = (uint32_t)u;
            per[n * 4 + 1] = (uint32_t)s;
            per[n * 4 + 2] = (uint32_t)id;
            per[n * 4 + 3] = (uint32_t)ni;
            n++;
        }
    }
    return n;
}

struct meminfo {
    uint64_t total, free, used;
};

static void read_mem(struct meminfo *m)
{
    char buf[1024];
    memset(m, 0, sizeof *m);
    if (read_text("/proc/meminfo", buf, sizeof buf) > 0) {
        m->total = (uint64_t)status_field(buf, "MemTotal:") * 1024;
        m->free = (uint64_t)status_field(buf, "MemFree:") * 1024;
        m->used = (uint64_t)status_field(buf, "MemUsed:") * 1024;
    }
}

static int d_host_processor_info(uint32_t host, int flavor, uint32_t *count, uint32_t **info, uint32_t *icount)
{
    (void)host;
    if (flavor != 2)
        return KERN_INVALID_ARGUMENT;
    int n = ncpu();
    uint32_t tot[4];
    uint32_t *a = mmap(NULL, (size_t)n * 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (a == MAP_FAILED)
        return KERN_FAILURE;
    int got = cpu_ticks(a, n, tot);
    if (!got) {
        a[0] = tot[0];
        a[1] = tot[1];
        a[2] = tot[2];
        a[3] = tot[3];
        got = 1;
    }
    *count = (uint32_t)got;
    *info = a;
    *icount = (uint32_t)got * 4;
    return KERN_SUCCESS;
}

static int d_host_statistics(uint32_t host, int flavor, uint32_t *info, uint32_t *count)
{
    (void)host;
    if (flavor == 3 && *count >= 4) {
        uint32_t per[4 * 64];
        cpu_ticks(per, 64, info);
        *count = 4;
        return KERN_SUCCESS;
    }
    if (flavor == 1 && *count >= 6) {
        double l[3] = { 0, 0, 0 };
        getloadavg(l, 3);
        for (int i = 0; i < 3; i++) {
            info[i] = (uint32_t)(l[i] * 1000);
            info[3 + i] = 0;
        }
        *count = 6;
        return KERN_SUCCESS;
    }
    if (flavor == 2 && *count >= 15) {
        struct meminfo m;
        read_mem(&m);
        memset(info, 0, *count * 4);
        info[0] = (uint32_t)(m.free / 4096);
        info[1] = (uint32_t)(m.used / 4096);
        *count = 15;
        return KERN_SUCCESS;
    }
    return KERN_INVALID_ARGUMENT;
}

static int d_host_statistics64(uint32_t host, int flavor, uint32_t *info, uint32_t *count)
{
    (void)host;
    if (flavor == 4) {
        struct meminfo m;
        read_mem(&m);
        uint32_t c = *count;
        memset(info, 0, (size_t)c * 4);
        if (c >= 4) {
            info[0] = (uint32_t)(m.free / 4096);
            info[1] = (uint32_t)(m.used / 4096);
        }
        if (c >= 36)
            info[35] = info[1];
        *count = c < 38 ? c : 38;
        return KERN_SUCCESS;
    }
    return d_host_statistics(host, flavor, info, count);
}

static int d_host_info(uint32_t host, int flavor, uint32_t *info, uint32_t *count)
{
    (void)host;
    if (flavor != 1 || *count < 12)
        return KERN_INVALID_ARGUMENT;
    struct meminfo m;
    read_mem(&m);
    int n = ncpu();
    memset(info, 0, *count * 4);
    info[0] = (uint32_t)n;
    info[1] = (uint32_t)n;
    info[2] = (uint32_t)(m.total > 0x80000000ull ? 0x80000000u : m.total);
    info[3] = 0x01000007;
    info[4] = 3;
    info[5] = 0;
    info[6] = (uint32_t)n;
    info[7] = (uint32_t)n;
    info[8] = (uint32_t)n;
    info[9] = (uint32_t)n;
    memcpy(&info[10], &m.total, 8);
    *count = 12;
    return KERN_SUCCESS;
}

static int d_host_page_size(uint32_t host, size_t *sz)
{
    (void)host;
    *sz = 4096;
    return KERN_SUCCESS;
}

static int d_host_get_clock_service(uint32_t host, int id, uint32_t *serv)
{
    (void)host;
    *serv = 0x300 + (uint32_t)id;
    return KERN_SUCCESS;
}

static int d_clock_get_time(uint32_t serv, uint32_t *ts)
{
    struct timespec t;
    clock_gettime((serv & 0xff) == 1 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &t);
    ts[0] = (uint32_t)t.tv_sec;
    ts[1] = (uint32_t)t.tv_nsec;
    return KERN_SUCCESS;
}

DSYM("_host_processor_info", d_host_processor_info);
DSYM("_host_statistics", d_host_statistics);
DSYM("_host_statistics64", d_host_statistics64);
DSYM("_host_info", d_host_info);
DSYM("_host_page_size", d_host_page_size);
DSYM("_host_get_clock_service", d_host_get_clock_service);
DSYM("_clock_get_time", d_clock_get_time);

static void fill_bsdinfo(uint8_t *b, const struct pinfo *pi)
{
    memset(b, 0, 136);
    put32(b, 0, 0x10);
    put32(b, 4, (uint32_t)dstate(pi->state));
    put32(b, 12, (uint32_t)pi->pid);
    put32(b, 16, (uint32_t)pi->ppid);
    put32(b, 20, (uint32_t)pi->uid);
    put32(b, 24, (uint32_t)pi->gid);
    put32(b, 28, (uint32_t)pi->uid);
    put32(b, 32, (uint32_t)pi->gid);
    put32(b, 36, (uint32_t)pi->uid);
    put32(b, 40, (uint32_t)pi->gid);
    memcpy(b + 48, pi->comm, 16);
    memcpy(b + 64, pi->comm, 16);
    put32(b, 100, (uint32_t)pi->pgid);
    put32(b, 108, 0xffffffffu);
    put32(b, 116, (uint32_t)pi->nice);
    put64(b, 120, pi->start_sec);
    put64(b, 128, pi->start_usec);
}

static void fill_taskinfo(uint8_t *b, const struct pinfo *pi)
{
    memset(b, 0, 96);
    put64(b, 0, pi->vsize);
    put64(b, 8, pi->rss);
    put64(b, 16, pi->runtime_ns);
    put32(b, 48, 1);
    put32(b, 84, pi->threads);
    put32(b, 88, pi->state == 'R' ? 1 : 0);
    put32(b, 92, pi->prio);
}

static int d_proc_pidinfo(int pid, int flavor, uint64_t arg, void *buf, int size)
{
    (void)arg;
    struct pinfo pi;
    if (read_pinfo(pid, &pi) < 0) {
        errno = ESRCH;
        return 0;
    }
    uint8_t tmp[256];
    int need;
    switch (flavor) {
    case 2: fill_bsdinfo(tmp, &pi); fill_taskinfo(tmp + 136, &pi); need = 232; break;
    case 3: fill_bsdinfo(tmp, &pi); need = 136; break;
    case 4: fill_taskinfo(tmp, &pi); need = 96; break;
    case 13:
        memset(tmp, 0, 64);
        put32(tmp, 0, (uint32_t)pi.pid);
        put32(tmp, 4, (uint32_t)pi.ppid);
        put32(tmp, 8, (uint32_t)pi.pgid);
        put32(tmp, 12, (uint32_t)dstate(pi.state));
        memcpy(tmp + 16, pi.comm, 16);
        put32(tmp, 36, (uint32_t)pi.uid);
        put32(tmp, 40, (uint32_t)pi.gid);
        put32(tmp, 44, (uint32_t)pi.uid);
        put32(tmp, 48, (uint32_t)pi.gid);
        need = 64;
        break;
    default:
        errno = EINVAL;
        return 0;
    }
    if (!buf || size < need) {
        errno = ENOMEM;
        return 0;
    }
    memcpy(buf, tmp, (size_t)need);
    return need;
}

static int d_proc_pid_rusage(int pid, int flavor, void *buf)
{
    (void)flavor;
    struct pinfo pi;
    if (read_pinfo(pid, &pi) < 0) {
        errno = ESRCH;
        return -1;
    }
    uint8_t *b = buf;
    memset(b, 0, 96);
    put64(b, 16, pi.runtime_ns);
    put64(b, 72, pi.rss);
    put64(b, 80, pi.rss);
    put64(b, 88, (pi.start_sec - boot_time()) * 1000000000ull);
    return 0;
}

static int d_proc_listpids(uint32_t type, uint32_t info, void *buf, int size)
{
    (void)type;
    (void)info;
    static int pids[4096];
    int n = list_pids(pids, 4096);
    if (!buf)
        return n * 4 + 64;
    int fit = size / 4 < n ? size / 4 : n;
    memcpy(buf, pids, (size_t)fit * 4);
    return fit * 4;
}

static int d_proc_listallpids(void *buf, int size)
{
    int r = d_proc_listpids(1, 0, buf, size);
    return buf ? r / 4 : r / 4;
}

DSYM("_proc_pidinfo", d_proc_pidinfo);
DSYM("_proc_pid_rusage", d_proc_pid_rusage);
DSYM("_proc_listpids", d_proc_listpids);
DSYM("_proc_listallpids", d_proc_listallpids);

static int d_getpriority(int which, int who) { return getpriority(which, (id_t)who); }
static int d_setpriority(int which, int who, int prio) { return setpriority(which, (id_t)who, prio); }
DSYM("_getpriority", d_getpriority);
DSYM("_setpriority", d_setpriority);

static char *d_devname_r(uint32_t dev, uint16_t type, char *buf, int len)
{
    (void)type;
    snprintf(buf, (size_t)len, dev == 0xffffffffu ? "??" : "tty%u", dev & 0xff);
    return buf;
}

static char *d_devname(uint32_t dev, uint16_t type)
{
    static char b[32];
    return d_devname_r(dev, type, b, sizeof b);
}

static int d_backtrace(void **a, int n) { (void)a; (void)n; return 0; }
static void d_backtrace_symbols_fd(void *const *a, int n, int fd) { (void)a; (void)n; (void)fd; }
static char **d_backtrace_symbols(void *const *a, int n) { (void)a; (void)n; return calloc(1, sizeof(char *)); }

DSYM("_devname_r", d_devname_r);
DSYM("_devname", d_devname);
DSYM("_backtrace", d_backtrace);
DSYM("_backtrace_symbols_fd", d_backtrace_symbols_fd);
DSYM("_backtrace_symbols", d_backtrace_symbols);

#define KIO_NOT_FOUND 0xe00002f0

static uint32_t d_kIOMainPortDefault;
DSYM("_kIOMainPortDefault", &d_kIOMainPortDefault);
DSYM("_kIOMasterPortDefault", &d_kIOMainPortDefault);

static void *d_io_null(void) { return NULL; }
static int d_io_not_found(void) { return (int)KIO_NOT_FOUND; }
static int d_io_zero(void) { return 0; }

static int d_IOServiceGetMatchingServices(uint32_t port, void *match, uint32_t *iter)
{
    (void)port;
    d_cf_release(match);
    if (iter)
        *iter = 0;
    return (int)KIO_NOT_FOUND;
}

static uint32_t d_IOServiceGetMatchingService(uint32_t port, void *match)
{
    (void)port;
    d_cf_release(match);
    return 0;
}

static void *d_IOServiceMatching(const char *name)
{
    (void)name;
    return d_cf_empty_dict();
}

DSYM("_IOServiceMatching", d_IOServiceMatching);
DSYM("_IOServiceNameMatching", d_IOServiceMatching);
DSYM("_IOServiceGetMatchingServices", d_IOServiceGetMatchingServices);
DSYM("_IOServiceGetMatchingService", d_IOServiceGetMatchingService);
DSYM("_IOIteratorNext", d_io_zero);
DSYM("_IOObjectRelease", d_io_zero);
DSYM("_IOObjectRetain", d_io_zero);
DSYM("_IORegistryEntryCreateCFProperties", d_io_not_found);
DSYM("_IORegistryEntryCreateCFProperty", d_io_null);
DSYM("_IORegistryEntryGetName", d_io_not_found);
DSYM("_IORegistryEntryGetParentEntry", d_io_not_found);
DSYM("_IOServiceOpen", d_io_not_found);
DSYM("_IOServiceClose", d_io_zero);
DSYM("_IOConnectCallStructMethod", d_io_not_found);
static void *d_IOPSCopyPowerSourcesInfo(void) { return d_cf_empty_dict(); }
static void *d_IOPSCopyPowerSourcesList(void *blob) { (void)blob; return d_cf_empty_array(); }
DSYM("_IOPSCopyPowerSourcesInfo", d_IOPSCopyPowerSourcesInfo);
DSYM("_IOPSCopyPowerSourcesList", d_IOPSCopyPowerSourcesList);
DSYM("_IOPSGetPowerSourceDescription", d_io_null);
DSYM("_IOHIDEventSystemClientCreate", d_io_null);
DSYM("_IOHIDEventSystemClientSetMatching", d_io_zero);
DSYM("_IOHIDEventSystemClientCopyServices", d_io_null);
DSYM("_IOHIDServiceClientCopyEvent", d_io_null);
DSYM("_IOHIDServiceClientCopyProperty", d_io_null);
DSYM("_IOHIDEventGetFloatValue", d_io_zero);
DSYM("_IOReportCopyChannelsInGroup", d_io_null);
DSYM("_IOReportMergeChannels", d_io_zero);
DSYM("_IOReportCreateSubscription", d_io_null);
DSYM("_IOReportCreateSamples", d_io_null);
DSYM("_IOReportCreateSamplesDelta", d_io_null);
DSYM("_IOReportChannelGetChannelName", d_io_null);
DSYM("_IOReportChannelGetGroup", d_io_null);
DSYM("_IOReportChannelGetSubGroup", d_io_null);
DSYM("_IOReportChannelGetUnitLabel", d_io_null);
DSYM("_IOReportSimpleGetIntegerValue", d_io_zero);
DSYM("_IOReportStateGetCount", d_io_zero);
DSYM("_IOReportStateGetNameForIndex", d_io_null);
DSYM("_IOReportStateGetResidency", d_io_zero);
DSYM("_CFReadStreamCreateWithFile", d_io_null);
DSYM("_CFReadStreamOpen", d_io_zero);
DSYM("_CFReadStreamClose", d_io_zero);
DSYM("_CFURLCreateWithFileSystemPath", d_io_null);
DSYM("_CFPropertyListCreateWithStream", d_io_null);
