#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/ioctl.h>
#include "darwin.h"

#define EVFILT_READ   (-1)
#define EVFILT_WRITE  (-2)
#define EVFILT_TIMER  (-7)
#define EVFILT_USER   (-10)

#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_ONESHOT  0x0010
#define EV_CLEAR    0x0020
#define EV_RECEIPT  0x0040
#define EV_DISPATCH 0x0080
#define EV_ERROR    0x4000
#define EV_EOF      0x8000

#define NOTE_TRIGGER    0x01000000u
#define NOTE_FFCTRLMASK 0xc0000000u
#define NOTE_FFAND      0x40000000u
#define NOTE_FFOR       0x80000000u
#define NOTE_FFCOPY     0xc0000000u
#define NOTE_FFLAGSMASK 0x00ffffffu
#define NOTE_SECONDS    0x1
#define NOTE_USECONDS   0x2
#define NOTE_NSECONDS   0x4

#define T_FD    1
#define T_TIMER 2
#define T_USER  3
#define T_WAKE  4

#define MAX_KQ 1024

struct kev {
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    uint64_t udata;
};

struct knote {
    int type;
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    uint64_t udata;
    int enabled;
    int live;
    int pending;
    int tfd;
    struct knote *next;
};

struct fdslot {
    int type;
    int fd;
    int in_epoll;
    struct knote rd, wr;
    struct fdslot *next;
};

struct kq {
    int epfd;
    int wake_fd;
    int wake_type;
    pthread_mutex_t lock;
    struct fdslot *fds;
    struct knote *notes;
};

static struct kq *g_kq[MAX_KQ];
static pthread_mutex_t g_kq_lock = PTHREAD_MUTEX_INITIALIZER;

static struct kq *kq_get(int fd)
{
    if (fd < 0 || fd >= MAX_KQ) {
        errno = EBADF;
        return NULL;
    }
    struct kq *k = g_kq[fd];
    if (!k)
        errno = EBADF;
    return k;
}

static int d_kqueue(void)
{
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0)
        return -1;
    if (ep >= MAX_KQ) {
        close(ep);
        errno = EMFILE;
        return -1;
    }
    struct kq *k = calloc(1, sizeof *k);
    if (!k) {
        close(ep);
        errno = ENOMEM;
        return -1;
    }
    k->epfd = ep;
    k->wake_type = T_WAKE;
    pthread_mutex_init(&k->lock, NULL);
    k->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    struct epoll_event e = { .events = EPOLLIN, .data.ptr = &k->wake_type };
    if (k->wake_fd < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, k->wake_fd, &e) < 0) {
        if (k->wake_fd >= 0)
            close(k->wake_fd);
        close(ep);
        free(k);
        return -1;
    }
    pthread_mutex_lock(&g_kq_lock);
    g_kq[ep] = k;
    pthread_mutex_unlock(&g_kq_lock);
    return ep;
}

static struct fdslot *slot_find(struct kq *k, int fd, int create)
{
    for (struct fdslot *s = k->fds; s; s = s->next)
        if (s->fd == fd)
            return s;
    if (!create)
        return NULL;
    struct fdslot *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->type = T_FD;
    s->fd = fd;
    s->rd.filter = EVFILT_READ;
    s->wr.filter = EVFILT_WRITE;
    s->next = k->fds;
    k->fds = s;
    return s;
}

static int slot_sync(struct kq *k, struct fdslot *s)
{
    uint32_t ev = 0;
    int clear = 1, any = 0;
    if (s->rd.live && s->rd.enabled) {
        ev |= EPOLLIN | EPOLLRDHUP;
        clear &= !!(s->rd.flags & EV_CLEAR);
        any = 1;
    }
    if (s->wr.live && s->wr.enabled) {
        ev |= EPOLLOUT;
        clear &= !!(s->wr.flags & EV_CLEAR);
        any = 1;
    }
    if (!any) {
        if (s->in_epoll) {
            epoll_ctl(k->epfd, EPOLL_CTL_DEL, s->fd, NULL);
            s->in_epoll = 0;
        }
        return 0;
    }
    if (clear)
        ev |= EPOLLET;
    struct epoll_event e = { .events = ev, .data.ptr = s };
    int op = s->in_epoll ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    int r = epoll_ctl(k->epfd, op, s->fd, &e);
    if (r < 0 && errno == ENOENT && op == EPOLL_CTL_MOD)
        r = epoll_ctl(k->epfd, EPOLL_CTL_ADD, s->fd, &e);
    else if (r < 0 && errno == EEXIST && op == EPOLL_CTL_ADD)
        r = epoll_ctl(k->epfd, EPOLL_CTL_MOD, s->fd, &e);
    if (r == 0)
        s->in_epoll = 1;
    return r;
}

static struct knote *note_find(struct kq *k, uint64_t ident, int16_t filter)
{
    for (struct knote *n = k->notes; n; n = n->next)
        if (n->live && n->ident == ident && n->filter == filter)
            return n;
    return NULL;
}

static int timer_arm(struct knote *n)
{
    int64_t v = n->data;
    struct itimerspec it;
    uint64_t ns;
    if (n->fflags & NOTE_SECONDS)
        ns = (uint64_t)v * 1000000000ull;
    else if (n->fflags & NOTE_USECONDS)
        ns = (uint64_t)v * 1000ull;
    else if (n->fflags & NOTE_NSECONDS)
        ns = (uint64_t)v;
    else
        ns = (uint64_t)v * 1000000ull;
    if (!ns)
        ns = 1;
    memset(&it, 0, sizeof it);
    it.it_value.tv_sec = (time_t)(ns / 1000000000ull);
    it.it_value.tv_nsec = (long)(ns % 1000000000ull);
    if (!(n->flags & EV_ONESHOT))
        it.it_interval = it.it_value;
    return timerfd_settime(n->tfd, 0, &it, NULL);
}

static void note_free(struct kq *k, struct knote *n)
{
    if (n->tfd > 0) {
        epoll_ctl(k->epfd, EPOLL_CTL_DEL, n->tfd, NULL);
        close(n->tfd);
        n->tfd = 0;
    }
    n->live = 0;
}

static void user_fflags(struct knote *n, uint32_t ff)
{
    uint32_t v = ff & NOTE_FFLAGSMASK;
    switch (ff & NOTE_FFCTRLMASK) {
    case NOTE_FFAND: n->fflags &= v; break;
    case NOTE_FFOR: n->fflags |= v; break;
    case NOTE_FFCOPY: n->fflags = v; break;
    }
}

static int apply(struct kq *k, const struct kev *c)
{
    if (c->filter == EVFILT_READ || c->filter == EVFILT_WRITE) {
        struct fdslot *s = slot_find(k, (int)c->ident, (c->flags & EV_ADD) != 0);
        if (!s) {
            return (c->flags & EV_ADD) ? ENOMEM : ENOENT;
        }
        struct knote *n = c->filter == EVFILT_READ ? &s->rd : &s->wr;
        if (c->flags & EV_DELETE) {
            if (!n->live)
                return ENOENT;
            n->live = 0;
            slot_sync(k, s);
            return 0;
        }
        if (c->flags & EV_ADD) {
            if (!n->live)
                n->enabled = 1;
            n->live = 1;
            n->ident = c->ident;
            n->flags = c->flags;
            n->fflags = c->fflags;
            n->udata = c->udata;
        } else if (!n->live) {
            return ENOENT;
        }
        if (c->flags & EV_ENABLE)
            n->enabled = 1;
        if (c->flags & EV_DISABLE)
            n->enabled = 0;
        return slot_sync(k, s) < 0 ? errno : 0;
    }
    if (c->filter != EVFILT_TIMER && c->filter != EVFILT_USER)
        return EINVAL;
    struct knote *n = note_find(k, c->ident, c->filter);
    if (c->flags & EV_DELETE) {
        if (!n)
            return ENOENT;
        note_free(k, n);
        return 0;
    }
    if (!n) {
        if (!(c->flags & EV_ADD))
            return ENOENT;
        for (n = k->notes; n && n->live; n = n->next)
            ;
        if (!n) {
            n = calloc(1, sizeof *n);
            if (!n)
                return ENOMEM;
            n->next = k->notes;
            k->notes = n;
        }
        memset(n, 0, offsetof(struct knote, next));
        n->type = c->filter == EVFILT_TIMER ? T_TIMER : T_USER;
        n->ident = c->ident;
        n->filter = c->filter;
        n->enabled = 1;
        n->live = 1;
        if (n->type == T_TIMER) {
            n->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
            struct epoll_event e = { .events = EPOLLIN, .data.ptr = n };
            if (n->tfd < 0 || epoll_ctl(k->epfd, EPOLL_CTL_ADD, n->tfd, &e) < 0) {
                int er = errno;
                note_free(k, n);
                return er;
            }
        }
    }
    if (c->flags & EV_ADD) {
        n->flags = c->flags;
        n->udata = c->udata;
        if (n->type == T_TIMER) {
            n->fflags = c->fflags;
            n->data = c->data;
            timer_arm(n);
        }
    }
    if (c->flags & EV_ENABLE)
        n->enabled = 1;
    if (c->flags & EV_DISABLE)
        n->enabled = 0;
    if (n->type == T_USER) {
        user_fflags(n, c->fflags);
        if (c->fflags & NOTE_TRIGGER) {
            n->pending = 1;
            eventfd_write(k->wake_fd, 1);
        }
    }
    return 0;
}

static void after_report(struct kq *k, struct knote *n, struct fdslot *s)
{
    if (n->flags & EV_ONESHOT) {
        if (s) {
            n->live = 0;
            slot_sync(k, s);
        } else {
            note_free(k, n);
        }
    } else if (n->flags & EV_DISPATCH) {
        n->enabled = 0;
        if (s)
            slot_sync(k, s);
    }
}

static int collect_user(struct kq *k, struct kev *out, int max, int got)
{
    for (struct knote *n = k->notes; n && got < max; n = n->next) {
        if (!n->live || n->type != T_USER || !n->pending || !n->enabled)
            continue;
        struct kev *e = &out[got++];
        e->ident = n->ident;
        e->filter = n->filter;
        e->flags = n->flags & ~(EV_ADD | EV_ENABLE);
        e->fflags = n->fflags;
        e->data = 0;
        e->udata = n->udata;
        if (n->flags & EV_CLEAR) {
            n->pending = 0;
            n->fflags = 0;
        }
        after_report(k, n, NULL);
    }
    return got;
}

static int kev_core(int kqfd, const struct kev *ch, int nch, struct kev *out, int nout, const struct timespec *ts)
{
    struct kq *k = kq_get(kqfd);
    if (!k)
        return -1;
    int got = 0;
    pthread_mutex_lock(&k->lock);
    for (int i = 0; i < nch; i++) {
        int e = apply(k, &ch[i]);
        if (e || (ch[i].flags & EV_RECEIPT)) {
            if (got < nout) {
                out[got] = ch[i];
                out[got].flags = EV_ERROR;
                out[got].data = e ? d_errno_lx2d(e) : 0;
                got++;
            } else if (e) {
                pthread_mutex_unlock(&k->lock);
                errno = e;
                return -1;
            }
        }
    }
    if (nout <= 0 || got) {
        pthread_mutex_unlock(&k->lock);
        return got;
    }
    got = collect_user(k, out, nout, 0);
    pthread_mutex_unlock(&k->lock);
    int tmo = -1;
    if (got)
        tmo = 0;
    else if (ts)
        tmo = (int)(ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000);
    struct epoll_event evs[64];
    int want = nout - got;
    if (want > 64)
        want = 64;
    int n = epoll_wait(k->epfd, evs, want, tmo);
    if (n < 0) {
        if (errno == EINTR && got)
            return got;
        return got ? got : -1;
    }
    pthread_mutex_lock(&k->lock);
    for (int i = 0; i < n && got < nout; i++) {
        int type = *(int *)evs[i].data.ptr;
        uint32_t ev = evs[i].events;
        if (type == T_WAKE) {
            eventfd_t v;
            eventfd_read(k->wake_fd, &v);
            got = collect_user(k, out, nout, got);
        } else if (type == T_FD) {
            struct fdslot *s = evs[i].data.ptr;
            if ((ev & (EPOLLIN | EPOLLHUP | EPOLLERR | EPOLLRDHUP)) && s->rd.live && s->rd.enabled && got < nout) {
                int avail = 0;
                if (ioctl(s->fd, FIONREAD, &avail) < 0)
                    avail = 1;
                struct kev *e = &out[got++];
                e->ident = s->rd.ident;
                e->filter = EVFILT_READ;
                e->flags = (s->rd.flags & (EV_ONESHOT | EV_CLEAR | EV_DISPATCH)) |
                           ((ev & (EPOLLHUP | EPOLLRDHUP)) ? EV_EOF : 0);
                e->fflags = 0;
                e->data = avail;
                e->udata = s->rd.udata;
                after_report(k, &s->rd, s);
            }
            if ((ev & (EPOLLOUT | EPOLLHUP | EPOLLERR)) && s->wr.live && s->wr.enabled && got < nout) {
                struct kev *e = &out[got++];
                e->ident = s->wr.ident;
                e->filter = EVFILT_WRITE;
                e->flags = (s->wr.flags & (EV_ONESHOT | EV_CLEAR | EV_DISPATCH)) |
                           ((ev & (EPOLLHUP | EPOLLERR)) ? EV_EOF : 0);
                e->fflags = 0;
                e->data = 65536;
                e->udata = s->wr.udata;
                after_report(k, &s->wr, s);
            }
        } else if (type == T_TIMER) {
            struct knote *t = evs[i].data.ptr;
            uint64_t cnt = 0;
            if (!t->live || read(t->tfd, &cnt, sizeof cnt) != sizeof cnt || !t->enabled)
                continue;
            struct kev *e = &out[got++];
            e->ident = t->ident;
            e->filter = EVFILT_TIMER;
            e->flags = t->flags & (EV_ONESHOT | EV_CLEAR | EV_DISPATCH);
            e->fflags = 0;
            e->data = (int64_t)cnt;
            e->udata = t->udata;
            after_report(k, t, NULL);
        }
    }
    pthread_mutex_unlock(&k->lock);
    return got;
}

struct dkevent {
    uintptr_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    intptr_t data;
    void *udata;
};

struct dkevent64 {
    uint64_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    int64_t data;
    uint64_t udata;
    uint64_t ext[2];
};

static int d_kevent(int kq, const struct dkevent *ch, int nch, struct dkevent *ev, int nev, const struct timespec *ts)
{
    struct kev *c = nch > 0 ? calloc((size_t)nch, sizeof *c) : NULL;
    struct kev *o = nev > 0 ? calloc((size_t)nev, sizeof *o) : NULL;
    if ((nch > 0 && !c) || (nev > 0 && !o)) {
        free(c);
        free(o);
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; i < nch; i++) {
        c[i].ident = ch[i].ident;
        c[i].filter = ch[i].filter;
        c[i].flags = ch[i].flags;
        c[i].fflags = ch[i].fflags;
        c[i].data = ch[i].data;
        c[i].udata = (uint64_t)(uintptr_t)ch[i].udata;
    }
    int r = kev_core(kq, c, nch, o, nev, ts);
    for (int i = 0; i < r; i++) {
        ev[i].ident = (uintptr_t)o[i].ident;
        ev[i].filter = o[i].filter;
        ev[i].flags = o[i].flags;
        ev[i].fflags = o[i].fflags;
        ev[i].data = (intptr_t)o[i].data;
        ev[i].udata = (void *)(uintptr_t)o[i].udata;
    }
    free(c);
    free(o);
    return r;
}

static int d_kevent64(int kq, const struct dkevent64 *ch, int nch, struct dkevent64 *ev, int nev,
                      unsigned flags, const struct timespec *ts)
{
    struct timespec zero = { 0, 0 };
    if (flags & 0x1)
        ts = &zero;
    struct kev *c = nch > 0 ? calloc((size_t)nch, sizeof *c) : NULL;
    struct kev *o = nev > 0 ? calloc((size_t)nev, sizeof *o) : NULL;
    if ((nch > 0 && !c) || (nev > 0 && !o)) {
        free(c);
        free(o);
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; i < nch; i++) {
        c[i].ident = ch[i].ident;
        c[i].filter = ch[i].filter;
        c[i].flags = ch[i].flags;
        c[i].fflags = ch[i].fflags;
        c[i].data = ch[i].data;
        c[i].udata = ch[i].udata;
    }
    int r = kev_core(kq, c, nch, o, nev, ts);
    for (int i = 0; i < r; i++) {
        memset(&ev[i], 0, sizeof ev[i]);
        ev[i].ident = o[i].ident;
        ev[i].filter = o[i].filter;
        ev[i].flags = o[i].flags;
        ev[i].fflags = o[i].fflags;
        ev[i].data = o[i].data;
        ev[i].udata = o[i].udata;
    }
    free(c);
    free(o);
    return r;
}

DSYM("_kqueue", d_kqueue);
DSYM("_kevent", d_kevent);
DSYM("_kevent64", d_kevent64);
