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
#define LOOKAHEAD_MS  20
#define FIFO_FRAMES   16384
#define MIX_CHUNK     256
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
    int16_t  *buf;
    uint64_t  wpos;
    uint64_t  rpos;
    uint32_t  inc;
    uint32_t  phase;
    int16_t   prev_l;
    int16_t   prev_r;
    int       have_prev;
    uint8_t   carry[32];
    uint32_t  ncarry;
    uint32_t  frag_frames;
    uint32_t  nfrags;
    uint64_t  consumed;
    uint64_t  frags_seen;
    uint64_t  tail;
    int       active;
    int       draining;
    uint64_t  pend_since;
    uint32_t  xruns;
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
    uint32_t   lookahead;
    uint64_t   hw;
    uint64_t   mix;
    uint32_t   last_ptr;
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

static uint32_t fifo_fill(struct pcm_stream *s) {
    return (uint32_t)(s->wpos - s->rpos);
}

static uint32_t fifo_free(struct pcm_stream *s) {
    return FIFO_FRAMES - fifo_fill(s);
}

static void fifo_push(struct pcm_stream *s, int32_t l, int32_t r) {
    int16_t *d = s->buf + (s->wpos % FIFO_FRAMES) * 2;
    d[0] = (int16_t)l;
    d[1] = (int16_t)r;
    s->wpos++;
}

static void push_frame(struct pcm_stream *s, int32_t l, int32_t r) {
    if (s->inc == 65536) {
        fifo_push(s, l, r);
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
        fifo_push(s, ol, orr);
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

static uint32_t user_frames_for(struct pcm_stream *s, uint32_t dev_frames) {
    return (uint32_t)(((uint64_t)dev_frames * s->inc) >> 16);
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

static int device_prepare_locked(uint32_t rate) {
    uint32_t r = g_be->fix_rate(rate);
    uint32_t period = (r * DEV_BPF * DEV_PERIOD_MS / 1000 + 127) & ~127u;
    if (period < 256) period = 256;
    if (period > g_be->ring_max / 4) period = g_be->ring_max / 4;
    uint32_t periods = g_be->ring_max / period;
    if (periods > g_be->max_periods) periods = g_be->max_periods;
    if (periods < 4) periods = 4;
    if (g_be->prepare(r, &period, &periods) < 0) return -EIO;
    D.rate = r;
    D.period = period;
    D.periods = periods;
    D.ring = period * periods;
    uint32_t la = r * DEV_BPF * LOOKAHEAD_MS / 1000;
    if (la < 3 * period) la = 3 * period;
    if (la > D.ring / 2) la = D.ring / 2;
    D.lookahead = la & ~3u;
    memset(g_be->ring, 0, D.ring);
    D.hw = D.mix = 0;
    D.last_ptr = 0;
    D.running = 0;
    D.prepared = 1;
    for (int i = 0; i < PCM_MAX_STREAMS; i++) {
        if (!S[i].used) continue;
        update_inc(&S[i]);
        S[i].tail = 0;
    }
    return 0;
}

static void device_start_locked(void) {
    if (D.running || !D.prepared) return;
    g_be->trigger(1);
    D.running = 1;
    D.last_ptr = g_be->pointer() % D.ring;
    D.idle_since = 0;
    waitq_wake_all(&D.wq_worker);
}

static void device_stop_locked(void) {
    if (D.running) g_be->trigger(0);
    D.running = 0;
    D.prepared = 0;
}

static void update_hw_locked(void) {
    if (!D.running) return;
    uint32_t ptr = g_be->pointer() % D.ring;
    uint32_t delta = (ptr + D.ring - D.last_ptr) % D.ring;
    if (delta) {
        zero_ring(D.last_ptr, delta);
        D.hw += delta;
        D.last_ptr = ptr;
    }
    if (D.mix < D.hw) D.mix = D.hw;
    if (g_be->tick) g_be->tick();
}

static void mix_frames(uint8_t *dst, uint64_t at, uint32_t frames) {
    int32_t acc[MIX_CHUNK * 2];
    int16_t *out = (int16_t *)dst;
    while (frames) {
        uint32_t n = frames > MIX_CHUNK ? MIX_CHUNK : frames;
        memset(acc, 0, n * 2 * sizeof(int32_t));
        for (int i = 0; i < PCM_MAX_STREAMS; i++) {
            struct pcm_stream *s = &S[i];
            if (!s->used || !s->trig || !s->active) continue;
            uint32_t have = fifo_fill(s);
            uint32_t k = have < n ? have : n;
            for (uint32_t j = 0; j < k; j++) {
                const int16_t *f = s->buf + ((s->rpos + j) % FIFO_FRAMES) * 2;
                int32_t l = f[0], r = f[1];
                if (s->vol_l != 100) l = l * s->vol_l / 100;
                if (s->vol_r != 100) r = r * s->vol_r / 100;
                acc[j * 2]     += l;
                acc[j * 2 + 1] += r;
            }
            s->rpos += k;
            s->consumed += k;
            if (k) s->tail = at + (uint64_t)k * DEV_BPF;
            if (k < n) {
                s->active = 0;
                s->pend_since = 0;
                if (!s->draining) s->xruns++;
            }
        }
        for (uint32_t j = 0; j < n * 2; j++) {
            int32_t v = acc[j];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[j] = (int16_t)v;
        }
        out += n * 2;
        at += (uint64_t)n * DEV_BPF;
        frames -= n;
    }
}

static void fill_locked(void) {
    if (!D.running) return;
    uint64_t target = D.hw + D.lookahead;
    while (D.mix < target) {
        uint32_t pos = (uint32_t)(D.mix % D.ring);
        uint32_t n = D.ring - pos;
        if (n > target - D.mix) n = (uint32_t)(target - D.mix);
        n &= ~3u;
        if (!n) break;
        mix_frames(g_be->ring + pos, D.mix, n / DEV_BPF);
        D.mix += n;
    }
}

static uint32_t start_frames(void) {
    uint32_t n = (D.lookahead + 2 * D.period) / DEV_BPF;
    return n > FIFO_FRAMES / 2 ? FIFO_FRAMES / 2 : n;
}

static void activate_locked(struct pcm_stream *s) {
    s->active = 1;
    s->pend_since = 0;
}

static void activate_due_locked(void) {
    uint64_t now = sched_now_ns();
    for (int i = 0; i < PCM_MAX_STREAMS; i++) {
        struct pcm_stream *s = &S[i];
        if (!s->used || !s->trig || s->active || !fifo_fill(s)) continue;
        if (!s->pend_since) s->pend_since = now;
        else if (now - s->pend_since >= LOOKAHEAD_MS * 1000000ULL) activate_locked(s);
    }
}

static int any_pending_locked(void) {
    for (int i = 0; i < PCM_MAX_STREAMS; i++) {
        struct pcm_stream *s = &S[i];
        if (!s->used) continue;
        if (fifo_fill(s) || s->tail > D.hw) return 1;
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
        activate_due_locked();
        fill_locked();
        if (!any_pending_locked()) {
            uint64_t now = sched_now_ns();
            if (!D.idle_since) D.idle_since = now;
            else if (now - D.idle_since > IDLE_STOP_NS) device_stop_locked();
        } else {
            D.idle_since = 0;
        }
        uint64_t frag = D.period ? D.hw / D.period : 0;
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
    int16_t *buf = malloc(FIFO_FRAMES * DEV_BPF);
    if (!buf) return NULL;
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    struct pcm_stream *s = NULL;
    for (int i = 0; i < PCM_MAX_STREAMS; i++)
        if (!S[i].used) { s = &S[i]; break; }
    if (!s) {
        spinlock_release_irqrestore(&D.lock, f);
        free(buf);
        return NULL;
    }
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->owner = owner;
    s->buf = buf;
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

void pcm_close(pcm_stream_t *s, int drain) {
    if (!s || !s->used) return;
    if (drain) pcm_sync(s);
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    int16_t *buf = s->buf;
    s->used = 0;
    s->owner = NULL;
    s->buf = NULL;
    waitq_wake_all(&s->wq);
    spinlock_release_irqrestore(&D.lock, f);
    free(buf);
}

int pcm_streams_open(void) {
    int n = 0;
    for (int i = 0; i < PCM_MAX_STREAMS; i++) n += S[i].used;
    return n;
}

uint32_t pcm_device_rate(void) { return D.prepared ? D.rate : 0; }

static void reset_locked(struct pcm_stream *s) {
    s->rpos = s->wpos;
    s->ncarry = 0;
    s->have_prev = 0;
    s->phase = 0;
    s->active = 0;
    s->draining = 0;
    s->pend_since = 0;
    s->tail = 0;
    s->consumed = 0;
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
    uint32_t cap = user_frames_for(s, FIFO_FRAMES);
    if (ff > cap / 2) ff = cap / 2;
    return ff;
}

static void after_push_locked(struct pcm_stream *s) {
    s->draining = 0;
    if (!s->active) {
        if (!s->pend_since) s->pend_since = sched_now_ns();
        if (s->trig && fifo_fill(s) >= start_frames()) activate_locked(s);
    }
    if (s->trig && !D.running) device_start_locked();
}

static int ensure_device_locked(struct pcm_stream *s) {
    if (!D.prepared) {
        if (device_prepare_locked(s->rate) < 0) return -EIO;
    }
    update_inc(s);
    return 0;
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
        uint32_t freef = fifo_free(s);
        uint32_t in_max = freef > 8 ? user_frames_for(s, freef - 8) : 0;
        if (!in_max) {
            if (s->trig) activate_locked(s);
            if (s->trig && !D.running) device_start_locked();
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
                after_push_locked(s);
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
        after_push_locked(s);
    }
    spinlock_release_irqrestore(&D.lock, f);
    if (!done && nonblock && len) return -EAGAIN;
    return (long)done;
}

void pcm_space(pcm_stream_t *s, pcm_space_t *sp) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_inc(s);
    uint32_t freef = fifo_free(s);
    uint32_t uf = freef > 8 ? user_frames_for(s, freef - 8) : 0;
    uint32_t ff = frag_frames_of(s);
    uint32_t cap = user_frames_for(s, FIFO_FRAMES - 8);
    sp->bytes = uf * s->ubpf;
    sp->fragsize = ff * s->ubpf;
    sp->fragstotal = ff ? cap / ff : 0;
    sp->fragments = ff ? uf / ff : 0;
    spinlock_release_irqrestore(&D.lock, f);
}

uint32_t pcm_delay(pcm_stream_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_hw_locked();
    uint32_t dev = fifo_fill(s);
    if (s->tail > D.hw) dev += (uint32_t)((s->tail - D.hw) / DEV_BPF);
    uint32_t d = user_frames_for(s, dev) * s->ubpf + s->ncarry;
    spinlock_release_irqrestore(&D.lock, f);
    return d;
}

void pcm_position(pcm_stream_t *s, uint64_t *played, uint32_t *ptr, uint32_t *frags_done) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    update_hw_locked();
    uint64_t dev = s->consumed;
    if (s->tail > D.hw) {
        uint64_t ahead = (s->tail - D.hw) / DEV_BPF;
        dev = dev > ahead ? dev - ahead : 0;
    }
    uint64_t uframes = (dev * s->inc) >> 16;
    *played = uframes * s->ubpf;
    uint32_t cap = user_frames_for(s, FIFO_FRAMES);
    *ptr = cap ? (uint32_t)(uframes % cap) * s->ubpf : 0;
    uint32_t ff = frag_frames_of(s);
    uint64_t frags = ff ? uframes / ff : 0;
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
    s->draining = 1;
    if (fifo_fill(s) && s->trig) {
        activate_locked(s);
        if (!D.running) device_start_locked();
    }
    uint32_t rate = D.prepared ? D.rate : 48000;
    uint64_t left_ms = (uint64_t)fifo_fill(s) * 1000 / rate + LOOKAHEAD_MS;
    uint64_t deadline = sched_now_ns() + (left_ms + 2000) * 1000000ULL;
    int r = 0;
    while (s->used && D.running && (fifo_fill(s) || s->tail > D.hw)) {
        if (sched_now_ns() > deadline) break;
        waitq_block(&s->wq, &D.lock, f, sched_now_ns() + 5000000ULL);
        f = spinlock_acquire_irqsave(&D.lock);
        if (me && (me->pending_kill || signal_pending_deliverable(me))) { r = -EINTR; break; }
        update_hw_locked();
    }
    s->active = 0;
    s->pend_since = 0;
    s->ncarry = 0;
    spinlock_release_irqrestore(&D.lock, f);
    return r;
}

void pcm_post(pcm_stream_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    if (fifo_fill(s) && s->trig && D.prepared) {
        activate_locked(s);
        device_start_locked();
    }
    spinlock_release_irqrestore(&D.lock, f);
}

void pcm_set_trigger(pcm_stream_t *s, int enable) {
    uint64_t f = spinlock_acquire_irqsave(&D.lock);
    s->trig = enable ? 1 : 0;
    if (s->trig && fifo_fill(s) && D.prepared) {
        activate_locked(s);
        device_start_locked();
    }
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
    int n = snprintf(buf, max, "device: prepared=%d running=%d rate=%u period=%u periods=%u lookahead=%u hw=%llu mix=%llu ptr=%u\n",
                     D.prepared, D.running, D.rate, D.period, D.periods, D.lookahead,
                     (unsigned long long)D.hw, (unsigned long long)D.mix, ptr);
    for (int i = 0; i < PCM_MAX_STREAMS && n >= 0 && (size_t)n < max; i++) {
        struct pcm_stream *s = &S[i];
        if (!s->used) continue;
        n += snprintf(buf + n, max - (size_t)n, "stream %d: rate=%u ch=%d fmt=0x%x fill=%u inc=%u trig=%d xruns=%u consumed=%llu\n",
                      i, s->rate, s->ch, s->fmt, fifo_fill(s), s->inc, s->trig, s->xruns,
                      (unsigned long long)s->consumed);
    }
    spinlock_release_irqrestore(&D.lock, f);
    return n;
}
