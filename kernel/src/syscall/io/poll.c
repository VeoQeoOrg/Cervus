#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/sched/sched.h"
#include "../../../include/signal/signal.h"
#include <string.h>

struct k_pollfd { int fd; short events; short revents; };
struct k_timeval { long tv_sec; long tv_usec; };
struct k_timespec { long tv_sec; long tv_nsec; };

extern void     task_sleep_ms(uint64_t ms);
extern uint64_t sched_now_ns(void);

#define POLL_MAX   256
#define FDS_BYTES  128

int vfs_poll_file(vfs_file_t *file, int events) {
    if (!file || !file->vnode) return POLLNVAL;
    if (file->vnode->ops && file->vnode->ops->poll_file)
        return file->vnode->ops->poll_file(file, events);
    if (file->vnode->ops && file->vnode->ops->poll)
        return file->vnode->ops->poll(file->vnode, events);
    return events & (POLLIN | POLLOUT);
}

static int vnode_poll(vnode_t *vn, int events) {
    if (vn && vn->ops && vn->ops->poll) return vn->ops->poll(vn, events);
    return POLLIN | POLLOUT;
}

static int do_poll(struct k_pollfd *pf, int n, int64_t timeout_ms) {
    task_t *me = syscall_cur_task();
    uint64_t start = sched_now_ns();
    for (;;) {
        int ready = 0;
        for (int i = 0; i < n; i++) {
            pf[i].revents = 0;
            if (pf[i].fd < 0) continue;
            vfs_file_t *file = (me && me->fd_table) ? fd_get(me->fd_table, pf[i].fd) : NULL;
            if (!file) { pf[i].revents = POLLNVAL; ready++; continue; }
            int rev = (file->vnode && file->vnode->ops && file->vnode->ops->poll_file)
                    ? file->vnode->ops->poll_file(file, pf[i].events)
                    : vnode_poll(file->vnode, pf[i].events);
            fd_put(file);
            rev = poll_expand(rev) & (pf[i].events | POLLERR | POLLHUP | POLLNVAL);
            if (rev) { pf[i].revents = (short)rev; ready++; }
        }
        if (ready > 0) return ready;
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && (sched_now_ns() - start) >= (uint64_t)timeout_ms * 1000000ULL) return 0;
        if (me && (me->pending_kill || signal_pending_deliverable(me))) return -EINTR;
        task_sleep_ms(5);
    }
}

int64_t sys_poll(uint64_t fds_ptr, uint64_t nfds, uint64_t timeout) {
    int n = (int)nfds;
    if (n < 0 || n > POLL_MAX) return -EINVAL;
    struct k_pollfd pf[POLL_MAX];
    size_t sz = (size_t)n * sizeof(struct k_pollfd);
    if (n > 0) {
        if (!syscall_uptr_validate((void *)fds_ptr, sz)) return -EFAULT;
        if (syscall_copy_from_user(pf, (void *)fds_ptr, sz) < 0) return -EFAULT;
    }
    int r = do_poll(pf, n, (int64_t)(int)timeout);
    if (r < 0) return r;
    if (n > 0 && syscall_copy_to_user((void *)fds_ptr, pf, sz) < 0) return -EFAULT;
    return r;
}

static int64_t do_select(int nfds, uint64_t rfds_, uint64_t wfds_, uint64_t efds_, int64_t timeout_ms) {
    if (nfds < 0 || nfds > POLL_MAX) return -EINVAL;
    int nb = (nfds + 7) / 8;
    unsigned char rd[FDS_BYTES], wr[FDS_BYTES], ex[FDS_BYTES];
    memset(rd, 0, sizeof rd); memset(wr, 0, sizeof wr); memset(ex, 0, sizeof ex);
    if (rfds_ && syscall_copy_from_user(rd, (void *)rfds_, nb) < 0) return -EFAULT;
    if (wfds_ && syscall_copy_from_user(wr, (void *)wfds_, nb) < 0) return -EFAULT;
    if (efds_ && syscall_copy_from_user(ex, (void *)efds_, nb) < 0) return -EFAULT;

    struct k_pollfd pf[POLL_MAX];
    int n = 0;
    for (int fd = 0; fd < nfds && n < POLL_MAX; fd++) {
        int ev = 0;
        if (rd[fd / 8] & (1 << (fd & 7))) ev |= POLLIN;
        if (wr[fd / 8] & (1 << (fd & 7))) ev |= POLLOUT;
        if (ex[fd / 8] & (1 << (fd & 7))) ev |= POLLPRI;
        if (!ev) continue;
        pf[n].fd = fd; pf[n].events = (short)ev; pf[n].revents = 0; n++;
    }

    int r = do_poll(pf, n, timeout_ms);
    if (r < 0) return r;

    memset(rd, 0, nb); memset(wr, 0, nb); memset(ex, 0, nb);
    int count = 0;
    for (int i = 0; i < n; i++) {
        int fd = pf[i].fd, re = pf[i].revents;
        if (re & (POLLIN | POLLHUP | POLLERR)) { rd[fd / 8] |= (1 << (fd & 7)); count++; }
        if (re & POLLOUT)                      { wr[fd / 8] |= (1 << (fd & 7)); count++; }
        if (re & POLLPRI)                      { ex[fd / 8] |= (1 << (fd & 7)); count++; }
    }
    if (rfds_ && syscall_copy_to_user((void *)rfds_, rd, nb) < 0) return -EFAULT;
    if (wfds_ && syscall_copy_to_user((void *)wfds_, wr, nb) < 0) return -EFAULT;
    if (efds_ && syscall_copy_to_user((void *)efds_, ex, nb) < 0) return -EFAULT;
    return count;
}

int64_t sys_select(uint64_t nfds_, uint64_t rfds_, uint64_t wfds_, uint64_t efds_, uint64_t tv_) {
    int64_t timeout_ms = -1;
    if (tv_) {
        struct k_timeval tv;
        if (syscall_copy_from_user(&tv, (void *)tv_, sizeof tv) < 0) return -EFAULT;
        if (tv.tv_sec < 0 || tv.tv_usec < 0) return -EINVAL;
        timeout_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }
    return do_select((int)nfds_, rfds_, wfds_, efds_, timeout_ms);
}

static int read_timespec_ms(uint64_t ts_, int64_t *out) {
    *out = -1;
    if (!ts_) return 0;
    struct k_timespec ts;
    if (syscall_copy_from_user(&ts, (void *)ts_, sizeof ts) < 0) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) return -EINVAL;
    *out = (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec + 999999) / 1000000;
    return 0;
}

static int enter_sigmask(task_t *t, uint64_t mask_) {
    if (!mask_ || !t) return 0;
    uint64_t m;
    if (syscall_copy_from_user(&m, (void *)mask_, sizeof m) < 0) return -EFAULT;
    t->sig_saved_mask = t->sig_blocked;
    t->sig_saved_valid = 1;
    t->sig_blocked = m & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
    return 1;
}

static void leave_sigmask(task_t *t, int64_t r) {
    if (!t || !t->sig_saved_valid) return;
    if (r == -EINTR && signal_pending_deliverable(t)) return;
    t->sig_blocked = t->sig_saved_mask;
    t->sig_saved_valid = 0;
}

int64_t sys_ppoll(uint64_t fds_ptr, uint64_t nfds, uint64_t ts_, uint64_t mask_, uint64_t sigsetsize) {
    (void)sigsetsize;
    int n = (int)nfds;
    if (n < 0 || n > POLL_MAX) return -EINVAL;
    int64_t timeout_ms;
    int e = read_timespec_ms(ts_, &timeout_ms);
    if (e < 0) return e;
    struct k_pollfd pf[POLL_MAX];
    size_t sz = (size_t)n * sizeof(struct k_pollfd);
    if (n > 0) {
        if (!syscall_uptr_validate((void *)fds_ptr, sz)) return -EFAULT;
        if (syscall_copy_from_user(pf, (void *)fds_ptr, sz) < 0) return -EFAULT;
    }
    task_t *t = syscall_cur_task();
    e = enter_sigmask(t, mask_);
    if (e < 0) return e;
    int64_t r = do_poll(pf, n, timeout_ms);
    leave_sigmask(t, r);
    if (r < 0) return r;
    if (n > 0 && syscall_copy_to_user((void *)fds_ptr, pf, sz) < 0) return -EFAULT;
    return r;
}

int64_t sys_pselect(uint64_t nfds_, uint64_t rfds_, uint64_t wfds_, uint64_t efds_, uint64_t ts_, uint64_t mask_) {
    int64_t timeout_ms;
    int e = read_timespec_ms(ts_, &timeout_ms);
    if (e < 0) return e;
    task_t *t = syscall_cur_task();
    e = enter_sigmask(t, mask_);
    if (e < 0) return e;
    int64_t r = do_select((int)nfds_, rfds_, wfds_, efds_, timeout_ms);
    leave_sigmask(t, r);
    return r;
}
