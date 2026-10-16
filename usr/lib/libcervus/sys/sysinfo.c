#include <sys/sysinfo.h>
#include <sys/cervus.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int sysinfo(struct sysinfo *info)
{
    if (!info) { errno = EFAULT; return -1; }
    memset(info, 0, sizeof *info);
    cervus_meminfo_t m;
    if (cervus_meminfo(&m) == 0) {
        info->totalram = (unsigned long)m.usable_bytes;
        info->freeram  = (unsigned long)m.free_bytes;
    }
    info->uptime = (unsigned long)(cervus_uptime_ns() / 1000000000ULL);
    info->procs = 1;
    info->mem_unit = 1;
    return 0;
}

int get_nprocs_conf(void) { long n = sysconf(_SC_NPROCESSORS_CONF); return n > 0 ? (int)n : 1; }
int get_nprocs(void) { long n = sysconf(_SC_NPROCESSORS_ONLN); return n > 0 ? (int)n : 1; }
long get_phys_pages(void) { struct sysinfo s; sysinfo(&s); return (long)(s.totalram / 4096); }
long get_avphys_pages(void) { struct sysinfo s; sysinfo(&s); return (long)(s.freeram / 4096); }
