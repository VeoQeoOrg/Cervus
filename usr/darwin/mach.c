#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <resolv.h>
#include <sys/mman.h>
#include "darwin.h"

#define KERN_SUCCESS          0
#define KERN_INVALID_ADDRESS  1
#define KERN_FAILURE          5
#define KERN_NOT_SUPPORTED    46

static uint32_t d_mach_task_self_ = 0x103;
DSYM("_mach_task_self_", &d_mach_task_self_);

static uint32_t d_mach_host_self(void) { return 0x203; }
static uint32_t d_mach_thread_self(void) { return (uint32_t)gettid(); }
static int d_mach_port_deallocate(uint32_t task, uint32_t port) { (void)task; (void)port; return KERN_SUCCESS; }
static int d_mach_port_mod_refs(uint32_t t, uint32_t p, int r, int d) { (void)t; (void)p; (void)r; (void)d; return KERN_SUCCESS; }
static int d_kern_failure(void) { return KERN_FAILURE; }
static int d_kern_invalid_address(void) { return KERN_INVALID_ADDRESS; }

DSYM("_mach_host_self", d_mach_host_self);
DSYM("_mach_thread_self", d_mach_thread_self);
DSYM("_mach_port_deallocate", d_mach_port_deallocate);
DSYM("_mach_port_mod_refs", d_mach_port_mod_refs);
DSYM("_mach_vm_region", d_kern_invalid_address);
DSYM("_vm_region_64", d_kern_invalid_address);
DSYM("_mach_vm_region_recurse", d_kern_invalid_address);
DSYM("_task_for_pid", d_kern_failure);
DSYM("_task_threads", d_kern_failure);
DSYM("_thread_info", d_kern_failure);

static int d_vm_deallocate(uint32_t task, uintptr_t addr, size_t size)
{
    (void)task;
    return munmap((void *)addr, size) == 0 ? KERN_SUCCESS : KERN_INVALID_ADDRESS;
}
DSYM("_vm_deallocate", d_vm_deallocate);
DSYM("_mach_vm_deallocate", d_vm_deallocate);

static size_t d_vm_page_size = 4096;
static size_t d_vm_page_mask = 4095;
static int d_vm_page_shift = 12;
DSYM("_vm_page_size", &d_vm_page_size);
DSYM("_vm_page_mask", &d_vm_page_mask);
DSYM("_vm_page_shift", &d_vm_page_shift);
DSYM("_vm_kernel_page_size", &d_vm_page_size);
DSYM("_vm_kernel_page_mask", &d_vm_page_mask);

static int d_proc_name(int pid, char *buf, uint32_t size);

static int d_proc_pidpath(int pid, char *buf, uint32_t size)
{
    char link[64];
    snprintf(link, sizeof link, "/proc/%d/exe", pid);
    ssize_t n = readlink(link, buf, size ? size - 1 : 0);
    if (n <= 0)
        return d_proc_name(pid, buf, size);
    if (pid == getpid() && d_exec_path[0]) {
        n = (ssize_t)snprintf(buf, size, "%s", d_exec_path);
        return (int)n;
    }
    buf[n] = 0;
    return (int)n;
}

static int d_proc_regionfilename(int pid, uint64_t addr, char *buf, uint32_t size)
{
    (void)pid;
    (void)addr;
    (void)buf;
    (void)size;
    return 0;
}

static int d_proc_name(int pid, char *buf, uint32_t size)
{
    char path[64], name[256];
    snprintf(path, sizeof path, "/proc/%d/comm", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(name, sizeof name, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    name[strcspn(name, "\n")] = 0;
    return snprintf(buf, size, "%s", name);
}

DSYM("_proc_pidpath", d_proc_pidpath);
DSYM("_proc_regionfilename", d_proc_regionfilename);
DSYM("_proc_name", d_proc_name);

static int d_notify_is_valid_token(int t) { (void)t; return 0; }
static uint32_t d_notify_fail(void) { return 1000000; }
DSYM("_notify_is_valid_token", d_notify_is_valid_token);
DSYM("_notify_register_check", d_notify_fail);
DSYM("_notify_check", d_notify_fail);
DSYM("_notify_cancel", d_notify_fail);
DSYM("_notify_post", d_notify_fail);

static void *d_xpc_null(void) { return NULL; }
DSYM("_xpc_date_create_from_current", d_xpc_null);

static int d_ptrace(int req, pid_t pid, void *addr, int data)
{
    (void)pid;
    (void)addr;
    (void)data;
    if (req == 31)
        return 0;
    errno = EPERM;
    return -1;
}
DSYM("_ptrace", d_ptrace);

static int d_setgroups(int n, const gid_t *g) { return setgroups((size_t)n, g); }
DSYM("_setgroups", d_setgroups);

static int d_res_ninit(void *st) { (void)st; return 0; }
static void d_res_nclose(void *st) { (void)st; }

static int d_res_nsearch(void *st, const char *name, int cls, int type, unsigned char *ans, int len)
{
    (void)st;
    return res_search(name, cls, type, ans, len);
}

static int d_res_nquery(void *st, const char *name, int cls, int type, unsigned char *ans, int len)
{
    (void)st;
    return res_query(name, cls, type, ans, len);
}

DSYM("_res_9_ninit", d_res_ninit);
DSYM("_res_9_nclose", d_res_nclose);
DSYM("_res_9_ndestroy", d_res_nclose);
DSYM("_res_9_nsearch", d_res_nsearch);
DSYM("_res_9_nquery", d_res_nquery);
DSYM("_res_9_init", d_res_ninit);
DSYM("_res_9_search", res_search);
DSYM("_res_9_query", res_query);
