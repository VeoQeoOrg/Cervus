#include "../../../include/drivers/audio/audio.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/sched/waitq.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/io/serial.h"
#include <string.h>

extern uint64_t sched_now_ns(void);
extern void     task_sleep_ms(uint64_t ms);
extern task_t  *syscall_cur_task(void);
extern bool     signal_pending_deliverable(task_t *t);

#define DEV_BPF       4
#define DEF_PERIOD_MS 10

static const audio_backend_t *g_be;

static struct {
    spinlock_t lock;
    void      *owner;
    uint32_t   rate;
    int        ch;
    int        fmt;
    uint32_t   ubpf;
    uint32_t   frag_req;
    uint32_t   nfrag_req;
    int        prepared;
    int        running;
    int        trig;
    int        draining;
    uint32_t   period;
    uint32_t   periods;
    uint32_t   ring;
    uint64_t   appl;
    uint64_t   hw;
    uint32_t   last_ptr;
    uint8_t    carry[32];
    uint32_t   ncarry;
    int        vol_l;
    int        vol_r;
    uint32_t   xruns;
    uint64_t   frags_seen;
    waitq_t    wq;
    waitq_t    wq_worker;
} P = { .lock = SPINLOCK_INIT };

static uint32_t fmt_bytes(int fmt) {
    switch (fmt) {
        case PCM_FMT_U8:     return 1;
        case PCM_FMT_S16_LE: return 2;
        default:             return 4;
    }
}

static void recalc_ubpf(void) {
    P.ubpf = fmt_bytes(P.fmt) * (uint32_t)P.ch;
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

static void convert(const uint8_t *src, uint32_t frames, uint8_t *dst) {
    uint32_t sb = fmt_bytes(P.fmt);
    int16_t *out = (int16_t *)dst;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = sample_at(src, P.fmt);
        int32_t r = P.ch >= 2 ? sample_at(src + sb, P.fmt) : l;
        if (P.vol_l != 100) l = l * P.vol_l / 100;
        if (P.vol_r != 100) r = r * P.vol_r / 100;
        out[0] = (int16_t)l;
        out[1] = (int16_t)r;
        out += 2;
        src += P.ubpf;
    }
}

static void zero_ring(uint32_t pos, uint32_t n) {
    while (n) {
        uint32_t k = P.ring - pos;
        if (k > n) k = n;
        memset(g_be->ring + pos, 0, k);
        n -= k;
        pos = 0;
    }
}

static void stop_locked(void) {
    if (P.running) g_be->trigger(0);
    P.running = 0;
}

static void reset_locked(void) {
    stop_locked();
    P.prepared = 0;
    P.appl = P.hw = 0;
    P.ncarry = 0;
    waitq_wake_all(&P.wq);
}

static int prepare_locked(void) {
    uint32_t ring_max = g_be->ring_max;
    uint32_t period = P.frag_req ? P.frag_req : P.rate * DEV_BPF * DEF_PERIOD_MS / 1000;
    period = (period + 127) & ~127u;
    if (period < 256) period = 256;
    if (period > ring_max / 2) period = ring_max / 2;
    uint32_t periods = P.nfrag_req ? P.nfrag_req : ring_max / period;
    if (periods * period > ring_max) periods = ring_max / period;
    if (periods > g_be->max_periods) periods = g_be->max_periods;
    if (periods < 2) periods = 2;
    if (g_be->prepare(P.rate, &period, &periods) < 0) return -EIO;
    P.period = period;
    P.periods = periods;
    P.ring = period * periods;
    memset(g_be->ring, 0, P.ring);
    P.appl = P.hw = 0;
    P.last_ptr = 0;
    P.frags_seen = 0;
    P.running = 0;
    P.prepared = 1;
    return 0;
}

static void start_locked(void) {
    if (P.running || !P.prepared) return;
    g_be->trigger(1);
    P.running = 1;
    P.last_ptr = g_be->pointer() % P.ring;
    waitq_wake_all(&P.wq_worker);
}

static void maybe_start_locked(void) {
    if (P.running || !P.trig) return;
    uint32_t need = 2 * P.period;
    if (need > P.ring / 2) need = P.ring / 2;
    if (P.appl - P.hw >= need) start_locked();
}

static void update_locked(void) {
    if (!P.running) return;
    uint32_t ptr = g_be->pointer() % P.ring;
    uint32_t delta = (ptr + P.ring - P.last_ptr) % P.ring;
    if (delta) {
        zero_ring(P.last_ptr, delta);
        P.hw += delta;
        P.last_ptr = ptr;
    }
    if (P.appl < P.hw) {
        if (!P.draining) P.xruns++;
        P.appl = P.hw;
    }
    if (g_be->tick) g_be->tick();
}

static void pcm_worker(void *arg) {
    (void)arg;
    uint64_t last_frag = 0;
    for (;;) {
        uint64_t f = spinlock_acquire_irqsave(&P.lock);
        if (!P.running) {
            waitq_block(&P.wq_worker, &P.lock, f, 0);
            continue;
        }
        update_locked();
        uint64_t frag = P.period ? P.hw / P.period : 0;
        if (frag != last_frag) {
            last_frag = frag;
            waitq_wake_all(&P.wq);
        }
        spinlock_release_irqrestore(&P.lock, f);
        task_sleep_ms(2);
    }
}

void audio_register(const audio_backend_t *b) {
    if (!b || g_be) return;
    g_be = b;
    P.rate = b->fix_rate(48000);
    P.ch = 2;
    P.fmt = PCM_FMT_S16_LE;
    P.trig = 1;
    P.vol_l = P.vol_r = 100;
    recalc_ubpf();
    task_create("pcm", pcm_worker, NULL, IRQ_THREAD_PRIORITY);
    serial_printf("[audio] backend: %s\n", b->name);
    oss_init();
}

int audio_present(void) { return g_be != 0; }

const char *audio_backend_name(void) { return g_be ? g_be->name : "none"; }

int pcm_claim(void *owner) {
    if (!g_be) return -ENODEV;
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (P.owner && P.owner != owner) {
        spinlock_release_irqrestore(&P.lock, f);
        return -EBUSY;
    }
    P.owner = owner;
    reset_locked();
    P.fmt = PCM_FMT_U8;
    P.ch = 1;
    P.rate = g_be->fix_rate(8000);
    P.frag_req = 0;
    P.nfrag_req = 0;
    P.trig = 1;
    P.vol_l = P.vol_r = 100;
    P.xruns = 0;
    recalc_ubpf();
    spinlock_release_irqrestore(&P.lock, f);
    return 0;
}

void pcm_release(void *owner, int drain) {
    if (!g_be || !owner || P.owner != owner) return;
    if (drain) pcm_sync();
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (P.owner == owner) {
        reset_locked();
        P.owner = NULL;
    }
    spinlock_release_irqrestore(&P.lock, f);
}

void *pcm_owner(void) { return P.owner; }

uint32_t pcm_set_rate(uint32_t rate) {
    if (!g_be) return 0;
    if (rate < PCM_MIN_RATE) rate = PCM_MIN_RATE;
    if (rate > PCM_MAX_RATE) rate = PCM_MAX_RATE;
    uint32_t r = g_be->fix_rate(rate);
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (r != P.rate) {
        reset_locked();
        P.rate = r;
    }
    spinlock_release_irqrestore(&P.lock, f);
    return r;
}

int pcm_set_channels(int ch) {
    if (ch < 1) ch = 1;
    if (ch > PCM_MAX_CHANNELS) ch = PCM_MAX_CHANNELS;
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (ch != P.ch) {
        reset_locked();
        P.ch = ch;
        recalc_ubpf();
    }
    spinlock_release_irqrestore(&P.lock, f);
    return ch;
}

int pcm_set_format(int fmt) {
    if (!(fmt & PCM_FMTS) || (fmt & (fmt - 1))) return P.fmt;
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (fmt != P.fmt) {
        reset_locked();
        P.fmt = fmt;
        recalc_ubpf();
    }
    spinlock_release_irqrestore(&P.lock, f);
    return fmt;
}

uint32_t pcm_set_fragment(uint32_t frag_bytes, uint32_t nfrags) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    uint32_t frames = P.ubpf ? frag_bytes / P.ubpf : 0;
    reset_locked();
    P.frag_req = frames * DEV_BPF;
    P.nfrag_req = nfrags;
    uint32_t r = P.frag_req / DEV_BPF * P.ubpf;
    spinlock_release_irqrestore(&P.lock, f);
    return r;
}

long pcm_write(const void *buf, size_t len, int nonblock) {
    if (!g_be) return -ENODEV;
    const uint8_t *src = buf;
    size_t done = 0;
    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (!P.prepared && prepare_locked() < 0) {
        spinlock_release_irqrestore(&P.lock, f);
        return -EIO;
    }
    while (done < len) {
        update_locked();
        uint32_t space = (P.ring - (uint32_t)(P.appl - P.hw)) / DEV_BPF;
        if (!space) {
            if (!P.running) start_locked();
            if (nonblock) break;
            waitq_block(&P.wq, &P.lock, f, sched_now_ns() + 10000000ULL);
            if (me && (me->pending_kill || signal_pending_deliverable(me))) return done ? (long)done : -EINTR;
            f = spinlock_acquire_irqsave(&P.lock);
            if (!P.prepared && prepare_locked() < 0) break;
            continue;
        }
        if (P.ncarry) {
            uint32_t take = P.ubpf - P.ncarry;
            if (take > len - done) take = (uint32_t)(len - done);
            memcpy(P.carry + P.ncarry, src + done, take);
            P.ncarry += take;
            done += take;
            if (P.ncarry == P.ubpf) {
                convert(P.carry, 1, g_be->ring + (uint32_t)(P.appl % P.ring));
                P.appl += DEV_BPF;
                P.ncarry = 0;
            }
            continue;
        }
        uint32_t uframes = (uint32_t)((len - done) / P.ubpf);
        if (!uframes) {
            P.ncarry = (uint32_t)(len - done);
            memcpy(P.carry, src + done, P.ncarry);
            done = len;
            break;
        }
        uint32_t pos = (uint32_t)(P.appl % P.ring);
        uint32_t contig = (P.ring - pos) / DEV_BPF;
        uint32_t n = uframes;
        if (n > space) n = space;
        if (n > contig) n = contig;
        convert(src + done, n, g_be->ring + pos);
        P.appl += (uint64_t)n * DEV_BPF;
        done += (size_t)n * P.ubpf;
        maybe_start_locked();
    }
    spinlock_release_irqrestore(&P.lock, f);
    if (!done && nonblock && len) return -EAGAIN;
    return (long)done;
}

void pcm_space(pcm_space_t *s) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (!P.prepared && g_be) prepare_locked();
    update_locked();
    uint32_t ufr = P.ubpf;
    uint32_t free = P.prepared ? P.ring - (uint32_t)(P.appl - P.hw) : 0;
    s->bytes = free / DEV_BPF * ufr;
    s->fragsize = P.period / DEV_BPF * ufr;
    s->fragstotal = P.periods;
    s->fragments = s->fragsize ? s->bytes / s->fragsize : 0;
    spinlock_release_irqrestore(&P.lock, f);
}

uint32_t pcm_delay(void) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    update_locked();
    uint32_t d = (uint32_t)(P.appl - P.hw) / DEV_BPF * P.ubpf + P.ncarry;
    spinlock_release_irqrestore(&P.lock, f);
    return d;
}

void pcm_position(uint64_t *played, uint32_t *ptr, uint32_t *frags_done) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    update_locked();
    *played = P.hw / DEV_BPF * P.ubpf;
    *ptr = P.ring ? (uint32_t)(P.hw % P.ring) / DEV_BPF * P.ubpf : 0;
    uint64_t frags = P.period ? P.hw / P.period : 0;
    *frags_done = (uint32_t)(frags - P.frags_seen);
    P.frags_seen = frags;
    spinlock_release_irqrestore(&P.lock, f);
}

void pcm_reset(void) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    reset_locked();
    spinlock_release_irqrestore(&P.lock, f);
}

int pcm_sync(void) {
    if (!g_be) return -ENODEV;
    task_t *me = syscall_cur_task();
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (!P.prepared) {
        spinlock_release_irqrestore(&P.lock, f);
        return 0;
    }
    update_locked();
    if (P.appl > P.hw && !P.running) start_locked();
    uint64_t left_ms = (P.appl - P.hw) / DEV_BPF * 1000 / (P.rate ? P.rate : 48000);
    uint64_t deadline = sched_now_ns() + (left_ms + 2000) * 1000000ULL;
    int r = 0;
    P.draining = 1;
    while (P.running && P.appl > P.hw) {
        if (sched_now_ns() > deadline) break;
        waitq_block(&P.wq, &P.lock, f, sched_now_ns() + 10000000ULL);
        f = spinlock_acquire_irqsave(&P.lock);
        if (me && (me->pending_kill || signal_pending_deliverable(me))) { r = -EINTR; break; }
        update_locked();
    }
    P.draining = 0;
    stop_locked();
    P.prepared = 0;
    P.ncarry = 0;
    spinlock_release_irqrestore(&P.lock, f);
    return r;
}

void pcm_post(void) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    if (P.prepared && P.appl > P.hw) start_locked();
    spinlock_release_irqrestore(&P.lock, f);
}

void pcm_set_trigger(int enable) {
    uint64_t f = spinlock_acquire_irqsave(&P.lock);
    P.trig = enable ? 1 : 0;
    if (!P.trig) stop_locked();
    else if (P.prepared && P.appl > P.hw) start_locked();
    spinlock_release_irqrestore(&P.lock, f);
}

int pcm_get_trigger(void) { return P.trig; }

int pcm_get_volume(void) { return P.vol_l | (P.vol_r << 8); }

void pcm_set_volume(int lr) {
    int l = lr & 0xFF, r = (lr >> 8) & 0xFF;
    if (l > 100) l = 100;
    if (r > 100) r = 100;
    P.vol_l = l;
    P.vol_r = r;
}

int pcm_get_format(void) { return P.fmt; }
int pcm_get_channels(void) { return P.ch; }
uint32_t pcm_get_rate(void) { return P.rate; }
uint32_t pcm_xruns(void) { return P.xruns; }

int pcm_writable(void) {
    pcm_space_t s;
    pcm_space(&s);
    return s.bytes > 0;
}

int audio_open(uint32_t rate) {
    task_t *me = syscall_cur_task();
    int r = pcm_claim(me);
    if (r < 0) return r;
    pcm_set_format(PCM_FMT_S16_LE);
    pcm_set_channels(2);
    pcm_set_rate(rate);
    return 0;
}

long audio_write(const void *pcm, size_t bytes) {
    if (!g_be) return -ENODEV;
    if (P.owner != syscall_cur_task()) return -EBADF;
    return pcm_write(pcm, bytes, 0);
}

int audio_close(void) {
    if (!g_be) return -ENODEV;
    task_t *me = syscall_cur_task();
    if (P.owner != me) return -EBADF;
    pcm_release(me, 1);
    return 0;
}

void audio_set_owner(void *task) { (void)task; }

void audio_task_exit(void *task) {
    if (g_be && task && P.owner == task) pcm_release(task, 0);
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
