#include "../../../include/drivers/audio/audio.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/waitq.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/io/serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern uint64_t sched_now_ns(void);
extern void     task_sleep_ms(uint64_t ms);
extern task_t  *syscall_cur_task(void);
extern bool     signal_pending_deliverable(task_t *t);

#define DEV_BPF       4
#define DEV_PERIOD_MS 5
#define HOLD_FRAMES   16384
#define LATE_FRAMES   64
#define IDLE_STOP_NS  500000000ULL

struct pcm_stream {
    int       used;
    void     *owner;
    int       fmt;
    int       ch;
    uint32_t  rate;
    uint32_t  ubpf;
    int       vol_l;
    int       vol_r;
    int       trig;
    uint32_t  inc;
    uint32_t  phase;
    int16_t   prev_l;
    int16_t   prev_r;
    int       have_prev;
    uint8_t   carry[32];
    uint32_t  ncarry;
    uint32_t  frag_frames;
    uint32_t  nfrags;
    int       started;
    uint64_t  pos;
    uint64_t  written;
    uint64_t  frags_seen;
    uint32_t  xruns;
    int16_t  *hold;
    uint64_t  hold_w;
    uint64_t  hold_r;
    waitq_t   wq;
};

static const audio_backend_t *g_be;

static struct {
    spinlock_t lock;
    int        prepared;
    int        running;
    uint32_t   rate;
    uint32_t   period;
    uint32_t   periods;
    uint32_t   ring;
    uint32_t   frames;
    uint32_t   guard;
    uint64_t   hw;
    uint32_t   last_ptr;
    int32_t   *sum;
    uint32_t   sum_cap;
    uint64_t   idle_since;
    waitq_t    wq_worker;
} D = { .lock = SPINLOCK_INIT };

static struct pcm_stream S[PCM_MAX_STREAMS];

static uint32_t fmt_bytes(int fmt) {
    switch (fmt) {
        case PCM_FMT_U8:     return 1;
        case PCM_FMT_S16_LE: return 2;
        default:             return 4;
    }
}

static void recalc_ubpf(struct pcm_stream *s) {
    s->ubpf = fmt_bytes(s->fmt) * (uint32_t)s->ch;
}

static int32_t float_to_s16(uint32_t b) {
    int exp = (int)((b >> 23) & 0xFF);
    if (exp == 0) return 0;
    uint32_t man = (b & 0x7FFFFF) | 0x800000;
    int sh = exp - 135;
    int32_t v;
    if (sh >= 8) v = 32767;
    else if (sh >= 0) {
        uint64_t t = (uint64_t)man << sh;
        v = t > 32767 ? 32767 : (int32_t)t;
    } else if (sh > -32) {
        v = (int32_t)(man >> -sh);
        if (v > 32767) v = 32767;
    } else v = 0;
    return (b >> 31) ? -v : v;
}

static int32_t sample_at(const uint8_t *p, int fmt) {
    switch (fmt) {
        case PCM_FMT_U8:
            return ((int32_t)p[0] - 128) << 8;
        case PCM_FMT_S16_LE:
            return (int16_t)(uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        case PCM_FMT_S24_LE:
            return (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 16;
        case PCM_FMT_S32_LE:
            return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                             ((uint32_t)p[3] << 24)) >> 16;
        case PCM_FMT_FLOAT:
            return float_to_s16((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                                ((uint32_t)p[3] << 24));
        default:
            return 0;
    }
}

static uint32_t dev_rate_for(struct pcm_stream *s) {
    return D.prepared ? D.rate : g_be->fix_rate(s->rate);
}

static void update_inc(struct pcm_stream *s) {
    uint32_t dr = dev_rate_for(s);
    uint32_t inc = dr ? (uint32_t)(((uint64_t)s->rate << 16) / dr) : 65536;
    if (!inc) inc = 1;
    if (inc != s->inc) {
        s->inc = inc;
        s->phase = 0;
        s->have_prev = 0;
    }
}

static uint32_t user_frames_for(struct pcm_stream *s, uint64_t dev_frames) {
    return (uint32_t)((dev_frames * s->inc) >> 16);
}

static uint32_t hold_fill(struct pcm_stream *s) {
    return (uint32_t)(s->hold_w - s->hold_r);
}

static int16_t clip16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static void zero_ring(uint32_t pos, uint32_t n) {
    while (n) {
        uint32_t k = D.ring - pos;
        if (k > n) k = n;
        memset(g_be->ring + pos, 0, k);
        n -= k;
        pos = 0;
    }
}

static void zero_sum(uint64_t from, uint64_t n) {
    if (n > D.frames) n = D.frames;
    uint32_t idx = (uint32_t)(from % D.frames);
    while (n) {
        uint32_t k = D.frames - idx;
        if (k > n) k = (uint32_t)n;
        memset(D.sum + (size_t)idx * 2, 0, (size_t)k * 2 * sizeof(int32_t));
        n -= k;
        idx = 0;
    }
}

static void stop_streams_locked(void) {
    for (int i = 0; i < PCM_MAX_STREAMS; i++) S[i].started = 0;
}

static int device_prepare_locked(uint32_t rate) {
    uint32_t r = g_be->fix_rate(rate);
    uint32_t period = (r * DEV_BPF * DEV_PERIOD_MS / 1000 + 127) & ~127u;
    if (period < 256) period = 256;
    if (period > g_be->ring_max / 4) period = g_be->ring_max / 4;
    uint32_t periods = g_be->ring_max / period;
    if (periods > g_be->max_periods) periods = g_be->max_periods;
    if (periods < 4) periods = 4;
    if (g_be->prepare(r, &period, &periods) < 0) return -EIO;
    uint32_t frames = period * periods / DEV_BPF;
    if (frames > D.sum_cap) {
        int32_t *sum = malloc((size_t)frames * 2 * sizeof(int32_t));
        if (!sum) return -ENOMEM;
        free(D.sum);
        D.sum = sum;
        D.sum_cap = frames;
    }
    D.rate = r;
    D.period = period;
    D.periods = periods;
    D.ring = period * periods;
    D.frames = frames;
    D.guard = period / DEV_BPF + LATE_FRAMES;
    memset(g_be->ring, 0, D.ring);
    memset(D.sum, 0, (size_t)frames * 2 * sizeof(int32_t));
    D.hw = 0;
    D.last_ptr = 0;
    D.running = 0;
    D.prepared = 1;
    stop_streams_locked();
    for (int i = 0; i < PCM_MAX_STREAMS; i++)
        if (S[i].used) update_inc(&S[i]);
    return 0;
}

static void device_start_locked(void) {
    if (D.running || !D.prepared) return;
    g_be->trigger(1);
    D.running = 1;
    D.last_ptr = (g_be->pointer() % D.ring) & ~(DEV_BPF - 1);
    D.idle_since = 0;
    waitq_wake_all(&D.wq_worker);
}

static void device_stop_locked(void) {
    if (D.running) g_be->trigger(0);
    D.running = 0;
    D.prepared = 0;
    stop_streams_locked();
}

static void update_hw_locked(void) {
    if (!D.running) return;
    uint32_t ptr = (g_be->pointer() % D.ring) & ~(DEV_BPF - 1);
    uint32_t delta = (ptr + D.ring - D.last_ptr) % D.ring;
    if (delta) {
        zero_ring(D.last_ptr, delta);
        zero_sum(D.hw, delta / DEV_BPF);
        D.hw += delta / DEV_BPF;
        D.last_ptr = ptr;
    }
    if (g_be->tick) g_be->tick();
}

static uint64_t queued_of(struct pcm_stream *s) {
    return (s->started && s->pos > D.hw) ? s->pos - D.hw : 0;
}

static uint64_t limit_of(struct pcm_stream *s) {
    uint64_t lim = D.frames > D.guard + D.period / DEV_BPF ? D.frames - D.period / DEV_BPF : D.guard;
    if (s->frag_frames && s->nfrags) {
        uint64_t want = (((uint64_t)s->frag_frames * s->nfrags) << 16) / s->inc;
        if (want < D.guard + LATE_FRAMES) want = D.guard + LATE_FRAMES;
        if (want < lim) lim = want;
    }
    return lim;
}

static void sync_pos_locked(struct pcm_stream *s) {
    uint64_t start = D.hw + D.guard;
    if (!s->started) {
        s->pos = start;
        s->started = 1;
    } else if (s->pos < D.hw + LATE_FRAMES) {
        s->xruns++;
        s->pos = start;
    }
}

static void put_frame(struct pcm_stream *s, int32_t l, int32_t r) {
    if (s->vol_l != 100) l = l * s->vol_l / 100;
    if (s->vol_r != 100) r = r * s->vol_r / 100;
    uint32_t idx = (uint32_t)(s->pos % D.frames);
    int32_t *a = D.sum + (size_t)idx * 2;
    int16_t *o = (int16_t *)g_be->ring + (size_t)idx * 2;
    a[0] += l;
    a[1] += r;
    o[0] = clip16(a[0]);
    o[1] = clip16(a[1]);
    s->pos++;
    s->written++;
}

static void emit_frame(struct pcm_stream *s, int32_t l, int32_t r) {
    if (s->trig) {
        put_frame(s, l, r);
        return;
    }
    if (hold_fill(s) >= HOLD_FRAMES) return;
    int16_t *d = s->hold + (s->hold_w % HOLD_FRAMES) * 2;
    d[0] = (int16_t)l;
    d[1] = (int16_t)r;
    s->hold_w++;
}

static void push_frame(struct pcm_stream *s, int32_t l, int32_t r) {
    if (s->inc == 65536) {
        emit_frame(s, l, r);
        return;
    }
    if (!s->have_prev) {
        s->prev_l = (int16_t)l;
        s->prev_r = (int16_t)r;
        s->have_prev = 1;
        s->phase = 0;
    }
    while (s->phase < 65536) {
        int32_t ol = s->prev_l + (int32_t)(((int64_t)(l - s->prev_l) * s->phase) >> 16);
        int32_t orr = s->prev_r + (int32_t)(((int64_t)(r - s->prev_r) * s->phase) >> 16);
        emit_frame(s, ol, orr);
        s->phase += s->inc;
    }
    s->phase -= 65536;
    s->prev_l = (int16_t)l;
    s->prev_r = (int16_t)r;
}

static void convert_push(struct pcm_stream *s, const uint8_t *src, uint32_t frames) {
    uint32_t sb = fmt_bytes(s->fmt);
    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = sample_at(src, s->fmt);
        int32_t r = s->ch >= 2 ? sample_at(src + sb, s->fmt) : l;
        push_frame(s, l, r);
        src += s->ubpf;
    }
}

static void flush_hold_locked(struct pcm_stream *s) {
    if (!hold_fill(s) || !s->trig || !D.prepared) return;
    if (!D.running) device_start_locked();
    sync_pos_locked(s);
    uint64_t room = limit_of(s);
    uint64_t q = queued_of(s);
    room = room > q ? room - q : 0;
    while (hold_fill(s) && room--) {
        const int16_t *d = s->hold + (s->hold_r % HOLD_FRAMES) * 2;
        put_frame(s, d[0], d[1]);
        s->hold_r++;
    }
}

static int any_pending_locked(void) {
    for (int i = 0; i < PCM_MAX_STREAMS; i++) {
        struct pcm_stream *s = &S[i];
        if (s->used && (queued_of(s) || (s->trig && hold_fill(s)))) return 1;
    }
    return 0;
}

static void pcm_worker(void *arg) {
    (void)arg;
    uint64_t last_frag = 0;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&D.lock);
        if (!D.running) {
            waitq_block(&D.wq_worker, &D.lock, f, 0);
            continue;
        }
        update_hw_locked();
        for (int i = 0; i < PCM_MAX_STREAMS; i++)
            if (S[i].used && S[i].trig && hold_fill(&S[i])) flush_hold_locked(&S[i]);
        if (!any_pending_locked()) {
            uint64_t now = sched_now_ns();
            if (!D.idle_since) D.idle_since = now;
            else if (now - D.idle_since > IDLE_STOP_NS) device_stop_locked();
        } else {
            D.idle_since = 0;
        }
        uint64_t frag = D.period ? D.hw * DEV_BPF / D.period : 0;
        if (frag != last_frag) {
            last_frag = frag;
            for (int i = 0; i < PCM_MAX_STREAMS; i++)
                if (S[i].used) waitq_wake_all(&S[i].wq);
        }
        spinlock_release_irqrestore(&D.lock, f);
        task_sleep_ms(1);
    }
}

static int g_worker_started;

void audio_register(const audio_backend_t *b) {
    if (!b || g_be) return;
    g_be = b;
    serial_printf("[audio] backend: %s\n", b->name);
    oss_init();
}

int audio_present(void) { return g_be != 0; }

const char *audio_backend_name(void) { return g_be ? g_be->name : "none"; }

pcm_stream_t *pcm_open(void *owner) {
    if (!g_be || !owner) return NULL;
    if (!__atomic_exchange_n(&g_worker_started, 1, __ATOMIC_ACQ_REL))
        task_create("pcm", pcm_worker, NULL, IRQ_THREAD_PRIORITY);
    int16_t *hold = malloc(HOLD_FRAMES * DEV_BPF);
    if (!hold) return NULL;
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    struct pcm_stream *s = NULL;
    for (int i = 0; i < PCM_MAX_STREAMS; i++)
        if (!S[i].used) { s = &S[i]; break; }
    if (!s) {
        spinlock_release_irqrestore(&D.lock, f);
        free(hold);
        return NULL;
    }
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->owner = owner;
    s->hold = hold;
    s->fmt = PCM_FMT_U8;
    s->ch = 1;
    s->rate = 8000;
    s->vol_l = s->vol_r = 100;
    s->trig = 1;
    recalc_ubpf(s);
    update_inc(s);
    spinlock_release_irqrestore(&D.lock, f);
    return s;
}

pcm_stream_t *pcm_find(void *owner) {
    if (!owner) return NULL;
    for (int i = 0; i < PCM_MAX_STREAMS; i++)
        if (S[i].used && S[i].owner == owner) return &S[i];
    return NULL;
}

static int others_queued_locked(struct pcm_stream *s) {
    for (int i = 0; i < PCM_MAX_STREAMS; i++)
        if (&S[i] != s && S[i].used && queued_of(&S[i])) return 1;
    return 0;
}

static void drop_queued_locked(struct pcm_stream *s) {
    if (!s->started || !D.running) return;
    update_hw_locked();
    uint64_t from = D.hw + LATE_FRAMES;
    if (s->pos > from && !others_queued_locked(s)) {
        uint64_t n = s->pos - from;
        zero_sum(from, n);
        uint32_t byte = (uint32_t)((from % D.frames) * DEV_BPF);
        zero_ring(byte, (uint32_t)(n > D.frames ? D.frames : n) * DEV_BPF);
        s->written -= n;
    }
    s->started = 0;
}

void pcm_close(pcm_stream_t *s, int drain) {
    if (!s || !s->used) return;
    if (drain) pcm_sync(s);
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    int16_t *hold = s->hold;
    if (!drain) drop_queued_locked(s);
    s->used = 0;
    s->owner = NULL;
    s->hold = NULL;
    s->started = 0;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&D.lock, f);
    free(hold);
}

int pcm_streams_open(void) {
    int n = 0;
    for (int i = 0; i < PCM_MAX_STREAMS; i++) n += S[i].used;
    return n;
}

uint32_t pcm_device_rate(void) { return D.prepared ? D.rate : 0; }

static void reset_locked(struct pcm_stream *s) {
    drop_queued_locked(s);
    s->hold_r = s->hold_w;
    s->ncarry = 0;
    s->have_prev = 0;
    s->phase = 0;
    s->written = 0;
    s->frags_seen = 0;
    waitq_wake_all(&s->wq);
}

uint32_t pcm_set_rate(pcm_stream_t *s, uint32_t rate) {
    if (rate < PCM_MIN_RATE) rate = PCM_MIN_RATE;
    if (rate > PCM_MAX_RATE) rate = PCM_MAX_RATE;
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    if (rate != s->rate) {
        reset_locked(s);
        s->rate = rate;
        update_inc(s);
    }
    spinlock_release_irqrestore(&D.lock, f);
    return rate;
}

int pcm_set_channels(pcm_stream_t *s, int ch) {
    if (ch < 1) ch = 1;
    if (ch > PCM_MAX_CHANNELS) ch = PCM_MAX_CHANNELS;
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    if (ch != s->ch) {
        reset_locked(s);
        s->ch = ch;
        recalc_ubpf(s);
    }
    spinlock_release_irqrestore(&D.lock, f);
    return ch;
}

int pcm_set_format(pcm_stream_t *s, int fmt) {
    if (!(fmt & PCM_FMTS) || (fmt & (fmt - 1))) return s->fmt;
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    if (fmt != s->fmt) {
        reset_locked(s);
        s->fmt = fmt;
        recalc_ubpf(s);
    }
    spinlock_release_irqrestore(&D.lock, f);
    return fmt;
}

uint32_t pcm_set_fragment(pcm_stream_t *s, uint32_t frag_bytes, uint32_t nfrags) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    s->frag_frames = s->ubpf ? frag_bytes / s->ubpf : 0;
    s->nfrags = nfrags;
    uint32_t r = s->frag_frames * s->ubpf;
    spinlock_release_irqrestore(&D.lock, f);
    return r;
}

static uint32_t frag_frames_of(struct pcm_stream *s) {
    uint32_t ff = s->frag_frames ? s->frag_frames : s->rate / 100;
    if (ff < 64) ff = 64;
    uint32_t cap = D.prepared ? user_frames_for(s, limit_of(s)) : HOLD_FRAMES;
    if (ff > cap / 2) ff = cap / 2;
    if (!ff) ff = 1;
    return ff;
}

static int ensure_device_locked(struct pcm_stream *s) {
    if (!D.prepared) {
        if (device_prepare_locked(s->rate) < 0) return -EIO;
    }
    update_inc(s);
    return 0;
}

static uint32_t room_user_locked(struct pcm_stream *s) {
    if (!s->trig) {
        uint32_t free_hold = HOLD_FRAMES - hold_fill(s);
        return free_hold > 8 ? user_frames_for(s, free_hold - 8) : 0;
    }
    if (hold_fill(s)) return 0;
    if (!D.running) device_start_locked();
    update_hw_locked();
    sync_pos_locked(s);
    uint64_t lim = limit_of(s);
    uint64_t q = queued_of(s);
    if (q + 4 >= lim) return 0;
    return user_frames_for(s, lim - q - 4);
}

long pcm_write(pcm_stream_t *s, const void *buf, size_t len, int nonblock) {
    if (!g_be || !s) return -ENODEV;
    const uint8_t *src = buf;
    size_t done = 0;
    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    if (!s->used || ensure_device_locked(s) < 0) {
        spinlock_release_irqrestore(&D.lock, f);
        return -EIO;
    }
    while (done < len) {
        uint32_t in_max = room_user_locked(s);
        if (!in_max) {
            if (nonblock) break;
            waitq_block(&s->wq, &D.lock, f, sched_now_ns() + 10000000ULL);
            if (me && (me->pending_kill || signal_pending_deliverable(me))) return done ? (long)done : -EINTR;
            f = spinlock_acquire_irqsave(&D.lock);
            if (!s->used) break;
            if (ensure_device_locked(s) < 0) break;
            continue;
        }
        if (s->ncarry) {
            uint32_t take = s->ubpf - s->ncarry;
            if (take > len - done) take = (uint32_t)(len - done);
            memcpy(s->carry + s->ncarry, src + done, take);
            s->ncarry += take;
            done += take;
            if (s->ncarry == s->ubpf) {
                convert_push(s, s->carry, 1);
                s->ncarry = 0;
            }
            continue;
        }
        uint32_t uframes = (uint32_t)((len - done) / s->ubpf);
        if (!uframes) {
            s->ncarry = (uint32_t)(len - done);
            memcpy(s->carry, src + done, s->ncarry);
            done = len;
            break;
        }
        uint32_t n = uframes;
        if (n > in_max) n = in_max;
        if (n > 1024) n = 1024;
        convert_push(s, src + done, n);
        done += (size_t)n * s->ubpf;
    }
    spinlock_release_irqrestore(&D.lock, f);
    if (!done && nonblock && len) return -EAGAIN;
    return (long)done;
}

void pcm_space(pcm_stream_t *s, pcm_space_t *sp) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_inc(s);
    update_hw_locked();
    uint32_t ff = frag_frames_of(s);
    uint32_t cap, uf;
    if (D.prepared) {
        uint64_t lim = limit_of(s);
        uint64_t q = queued_of(s) + hold_fill(s);
        cap = user_frames_for(s, lim);
        uf = q + 4 < lim ? user_frames_for(s, lim - q - 4) : 0;
    } else {
        cap = HOLD_FRAMES;
        uf = HOLD_FRAMES - hold_fill(s);
    }
    sp->bytes = uf * s->ubpf;
    sp->fragsize = ff * s->ubpf;
    sp->fragstotal = cap / ff;
    sp->fragments = uf / ff;
    spinlock_release_irqrestore(&D.lock, f);
}

uint32_t pcm_delay(pcm_stream_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_hw_locked();
    uint64_t dev = queued_of(s) + hold_fill(s);
    uint32_t d = user_frames_for(s, dev) * s->ubpf + s->ncarry;
    spinlock_release_irqrestore(&D.lock, f);
    return d;
}

void pcm_position(pcm_stream_t *s, uint64_t *played, uint32_t *ptr, uint32_t *frags_done) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_hw_locked();
    uint64_t q = queued_of(s);
    uint64_t dev = s->written > q ? s->written - q : 0;
    uint64_t uframes = (dev * s->inc) >> 16;
    *played = uframes * s->ubpf;
    uint32_t ff = frag_frames_of(s);
    uint32_t cap = D.prepared ? user_frames_for(s, limit_of(s)) : HOLD_FRAMES;
    cap = cap / ff * ff;
    if (!cap) cap = ff;
    *ptr = (uint32_t)(uframes % cap) * s->ubpf;
    uint64_t frags = uframes / ff;
    *frags_done = (uint32_t)(frags - s->frags_seen);
    s->frags_seen = frags;
    spinlock_release_irqrestore(&D.lock, f);
}

void pcm_reset(pcm_stream_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    reset_locked(s);
    spinlock_release_irqrestore(&D.lock, f);
}

int pcm_sync(pcm_stream_t *s) {
    if (!g_be || !s) return -ENODEV;
    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    flush_hold_locked(s);
    uint32_t rate = D.prepared ? D.rate : 48000;
    uint64_t left_ms = (queued_of(s) + hold_fill(s)) * 1000 / rate;
    uint64_t deadline = sched_now_ns() + (left_ms + 2000) * 1000000ULL;
    int r = 0;
    while (s->used && D.running && (queued_of(s) || (s->trig && hold_fill(s)))) {
        if (sched_now_ns() > deadline) break;
        waitq_block(&s->wq, &D.lock, f, sched_now_ns() + 5000000ULL);
        f = spinlock_acquire_irqsave(&D.lock);
        if (me && (me->pending_kill || signal_pending_deliverable(me))) { r = -EINTR; break; }
        update_hw_locked();
        flush_hold_locked(s);
    }
    s->ncarry = 0;
    spinlock_release_irqrestore(&D.lock, f);
    return r;
}

void pcm_post(pcm_stream_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    flush_hold_locked(s);
    spinlock_release_irqrestore(&D.lock, f);
}

void pcm_set_trigger(pcm_stream_t *s, int enable) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    int was = s->trig;
    s->trig = enable ? 1 : 0;
    if (s->trig && !was) flush_hold_locked(s);
    if (!s->trig && was) drop_queued_locked(s);
    spinlock_release_irqrestore(&D.lock, f);
}

int pcm_get_trigger(pcm_stream_t *s) { return s->trig; }

int pcm_get_volume(pcm_stream_t *s) { return s->vol_l | (s->vol_r << 8); }

void pcm_set_volume(pcm_stream_t *s, int lr) {
    int l = lr & 0xFF, r = (lr >> 8) & 0xFF;
    if (l > 100) l = 100;
    if (r > 100) r = 100;
    s->vol_l = l;
    s->vol_r = r;
}

int pcm_get_format(pcm_stream_t *s) { return s->fmt; }
int pcm_get_channels(pcm_stream_t *s) { return s->ch; }
uint32_t pcm_get_rate(pcm_stream_t *s) { return s->rate; }
uint32_t pcm_xruns(pcm_stream_t *s) { return s->xruns; }

int pcm_writable(pcm_stream_t *s) {
    pcm_space_t sp;
    pcm_space(s, &sp);
    return sp.bytes >= sp.fragsize;
}

int audio_open(uint32_t rate) {
    task_t *me = syscall_cur_task();
    if (!g_be) return -ENODEV;
    if (pcm_find(me)) return -EBUSY;
    pcm_stream_t *s = pcm_open(me);
    if (!s) return -EBUSY;
    pcm_set_format(s, PCM_FMT_S16_LE);
    pcm_set_channels(s, 2);
    pcm_set_rate(s, rate);
    return 0;
}

long audio_write(const void *pcm, size_t bytes) {
    if (!g_be) return -ENODEV;
    pcm_stream_t *s = pcm_find(syscall_cur_task());
    if (!s) return -EBADF;
    return pcm_write(s, pcm, bytes, 0);
}

int audio_close(void) {
    if (!g_be) return -ENODEV;
    pcm_stream_t *s = pcm_find(syscall_cur_task());
    if (!s) return -EBADF;
    pcm_close(s, 1);
    return 0;
}

void audio_set_owner(void *task) { (void)task; }

void audio_task_exit(void *task) {
    if (!g_be || !task) return;
    pcm_stream_t *s = pcm_find(task);
    if (s) pcm_close(s, 0);
}

int audio_mixer_get(audio_mixer_t *m) {
    if (!g_be || !g_be->mixer_get || !m) return -1;
    return g_be->mixer_get(m);
}

int audio_set_volume(int pct) {
    if (!g_be || !g_be->set_volume) return -1;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return g_be->set_volume(pct);
}

int audio_set_mute(int mute) {
    if (!g_be || !g_be->set_mute) return -1;
    return g_be->set_mute(mute ? 1 : 0);
}

int audio_set_output(int idx) {
    if (!g_be || !g_be->set_output) return -1;
    return g_be->set_output(idx);
}

int pcm_status(char *buf, size_t max) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    uint32_t ptr = (g_be && D.prepared) ? g_be->pointer() : 0;
    int n = snprintf(buf, max, "device: prepared=%d running=%d rate=%u period=%u periods=%u guard=%u hw=%llu ptr=%u\n",
                     D.prepared, D.running, D.rate, D.period, D.periods, D.guard,
                     (unsigned long long)D.hw, ptr);
    for (int i = 0; i < PCM_MAX_STREAMS && n >= 0 && (size_t)n < max; i++) {
        struct pcm_stream *s = &S[i];
        if (!s->used) continue;
        n += snprintf(buf + n, max - (size_t)n, "stream %d: rate=%u ch=%d fmt=0x%x queued=%llu hold=%u inc=%u trig=%d xruns=%u written=%llu\n",
                      i, s->rate, s->ch, s->fmt, (unsigned long long)queued_of(s), hold_fill(s), s->inc, s->trig,
                      s->xruns, (unsigned long long)s->written);
    }
    spinlock_release_irqrestore(&D.lock, f);
    return n;
}
