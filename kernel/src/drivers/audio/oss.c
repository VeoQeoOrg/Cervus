#include "../../../include/drivers/audio/audio.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/devfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/sched/sched.h"
#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include <string.h>
#include <stdio.h>

#define KIOC(dir, type, nr, size) \
    (((uint64_t)(dir) << 30) | ((uint64_t)(size) << 16) | ((uint64_t)(type) << 8) | (uint64_t)(nr))
#define KIO(t, n)        KIOC(0, t, n, 0)
#define KIOR(t, n, sz)   KIOC(2, t, n, sz)
#define KIOW(t, n, sz)   KIOC(1, t, n, sz)
#define KIOWR(t, n, sz)  KIOC(3, t, n, sz)

typedef struct { int fragments, fragstotal, fragsize, bytes; } k_buf_info_t;
typedef struct { unsigned int bytes; int blocks; int ptr; } k_count_info_t;
typedef struct {
    int play_underruns, rec_overruns;
    unsigned int play_ptradjust, rec_ptradjust;
    int play_errorcount, rec_errorcount, play_lasterror, rec_lasterror, play_errorparm, rec_errorparm;
    int filler[16];
} k_errinfo_t;
typedef struct { long long samples; int fifo_samples; int filler[32]; } k_oss_count_t;

typedef struct {
    char product[32];
    char version[32];
    int  versionnum;
    char options[128];
    int  numaudios;
    int  openedaudio[8];
    int  numsynths;
    int  nummidis;
    int  numtimers;
    int  nummixers;
    int  openedmidi[8];
    int  numcards;
    int  numaudioengines;
    char license[16];
    char revision_info[256];
    int  filler[172];
} k_sysinfo_t;

typedef struct {
    int  dev;
    char name[64];
    int  busy;
    int  pid;
    int  caps;
    int  iformats, oformats;
    int  magic;
    char cmd[64];
    int  card_number;
    int  port_number;
    int  mixer_dev;
    int  legacy_device;
    int  enabled;
    int  flags;
    int  min_rate, max_rate;
    int  min_channels, max_channels;
    int  binding;
    int  rate_source;
    char handle[32];
    unsigned int nrates, rates[20];
    char song_name[64];
    char label[16];
    int  latency;
    char devnode[32];
    int  next_play_engine;
    int  next_rec_engine;
    int  filler[184];
} k_audioinfo_t;

typedef struct {
    int  dev;
    char id[16];
    char name[32];
    int  modify_counter;
    int  card_number;
    int  port_number;
    char handle[32];
    int  magic;
    int  enabled;
    int  caps;
    int  flags;
    int  nrext;
    int  priority;
    char devnode[32];
    int  legacy_device;
    int  filler[245];
} k_mixerinfo_t;

typedef struct {
    int  card;
    char shortname[16];
    char longname[128];
    int  flags;
    char hw_info[400];
    int  intr_count, ack_count;
    int  filler[154];
} k_cardinfo_t;

typedef struct {
    char id[16];
    char name[32];
    int  modify_counter;
    int  card_number;
    int  port_number;
    char handle[32];
} k_mixer_info_t;

#define DSP_HALT        KIO('P', 0)
#define DSP_SYNC        KIO('P', 1)
#define DSP_SPEED       KIOWR('P', 2, 4)
#define DSP_STEREO      KIOWR('P', 3, 4)
#define DSP_GETBLKSIZE  KIOWR('P', 4, 4)
#define DSP_SETFMT      KIOWR('P', 5, 4)
#define DSP_CHANNELS    KIOWR('P', 6, 4)
#define DSP_POST        KIO('P', 8)
#define DSP_SUBDIVIDE   KIOWR('P', 9, 4)
#define DSP_SETFRAGMENT KIOWR('P', 10, 4)
#define DSP_GETFMTS     KIOR('P', 11, 4)
#define DSP_GETOSPACE   KIOR('P', 12, sizeof(k_buf_info_t))
#define DSP_GETISPACE   KIOR('P', 13, sizeof(k_buf_info_t))
#define DSP_NONBLOCK    KIO('P', 14)
#define DSP_GETCAPS     KIOR('P', 15, 4)
#define DSP_GETTRIGGER  KIOR('P', 16, 4)
#define DSP_SETTRIGGER  KIOW('P', 16, 4)
#define DSP_GETIPTR     KIOR('P', 17, sizeof(k_count_info_t))
#define DSP_GETOPTR     KIOR('P', 18, sizeof(k_count_info_t))
#define DSP_SETSYNCRO   KIO('P', 21)
#define DSP_SETDUPLEX   KIO('P', 22)
#define DSP_GETODELAY   KIOR('P', 23, 4)
#define DSP_GETPLAYVOL  KIOR('P', 24, 4)
#define DSP_SETPLAYVOL  KIOWR('P', 24, 4)
#define DSP_GETERROR    KIOR('P', 25, sizeof(k_errinfo_t))
#define DSP_COOKEDMODE  KIOW('P', 30, 4)
#define DSP_SILENCE     KIO('P', 31)
#define DSP_SKIP        KIO('P', 32)
#define DSP_HALT_INPUT  KIO('P', 33)
#define DSP_HALT_OUTPUT KIO('P', 34)
#define DSP_LOW_WATER   KIOW('P', 34, 4)
#define DSP_CURRENT_OPTR KIOR('P', 35, sizeof(k_oss_count_t))
#define DSP_GET_PLAYTGT KIOR('P', 40, 4)
#define DSP_SET_PLAYTGT KIOWR('P', 40, 4)
#define DSP_POLICY      KIOW('P', 45, 4)
#define PCM_READ_RATE   KIOR('P', 2, 4)
#define PCM_READ_CHANNELS KIOR('P', 6, 4)
#define PCM_READ_BITS   KIOR('P', 5, 4)

#define X_SYSINFO       KIOR('X', 1, sizeof(k_sysinfo_t))
#define X_MIX_NRMIX     KIOR('X', 2, 4)
#define X_AUDIOINFO     KIOWR('X', 7, sizeof(k_audioinfo_t))
#define X_CARDINFO      KIOWR('X', 8, sizeof(k_cardinfo_t))
#define X_MIXERINFO     KIOWR('X', 10, sizeof(k_mixerinfo_t))
#define X_ENGINEINFO    KIOWR('X', 12, sizeof(k_audioinfo_t))
#define X_AUDIOINFO_EX  KIOWR('X', 13, sizeof(k_audioinfo_t))

#define MIX_INFO        KIOR('M', 101, sizeof(k_mixer_info_t))
#define OSS_GETVERSION  KIOR('M', 118, 4)

#define CAP_TRIGGER  0x00001000
#define CAP_REALTIME 0x00000200
#define CAP_OUTPUT   0x00020000
#define CAP_ANALOGOUT 0x00100000

#define MIXER_VOLUME 0
#define MIXER_PCM    4
#define MIXER_DEVMASK ((1 << MIXER_VOLUME) | (1 << MIXER_PCM))

static const uint32_t g_rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000 };

static int put_user(uint64_t uarg, const void *src, size_t n) {
    if (!uarg) return -EFAULT;
    return syscall_copy_to_user((void *)uarg, src, n) < 0 ? -EFAULT : 0;
}

static int get_user(void *dst, uint64_t uarg, size_t n) {
    if (!uarg) return -EFAULT;
    return syscall_copy_from_user(dst, (const void *)uarg, n) < 0 ? -EFAULT : 0;
}

static int get_int(uint64_t uarg, int *v) { return get_user(v, uarg, sizeof *v); }
static int put_int(uint64_t uarg, int v) { return put_user(uarg, &v, sizeof v); }

static void fill_audioinfo(k_audioinfo_t *ai) {
    memset(ai, 0, sizeof *ai);
    ai->dev = 0;
    snprintf(ai->name, sizeof ai->name, "Cervus %s", audio_backend_name());
    ai->busy = 0;
    ai->pid = -1;
    ai->caps = CAP_OUTPUT | CAP_TRIGGER | CAP_REALTIME | CAP_ANALOGOUT | 1;
    ai->oformats = PCM_FMTS;
    ai->iformats = 0;
    ai->card_number = 0;
    ai->port_number = 0;
    ai->mixer_dev = 0;
    ai->legacy_device = 0;
    ai->enabled = 1;
    ai->min_rate = 8000;
    ai->max_rate = 96000;
    ai->min_channels = 1;
    ai->max_channels = 2;
    ai->binding = 1;
    snprintf(ai->handle, sizeof ai->handle, "cervus-pcm0");
    ai->nrates = sizeof g_rates / sizeof g_rates[0];
    for (unsigned i = 0; i < ai->nrates; i++) ai->rates[i] = g_rates[i];
    ai->latency = -1;
    strcpy(ai->devnode, "/dev/dsp");
    ai->next_play_engine = 0;
    ai->next_rec_engine = 0;
    for (int i = 0; i < 184; i++) ai->filler[i] = -1;
}

static int64_t common_ioctl(uint64_t req, uint64_t arg) {
    switch (req) {
        case X_SYSINFO: {
            k_sysinfo_t si;
            memset(&si, 0, sizeof si);
            strcpy(si.product, "Cervus OSS");
            strcpy(si.version, "4.1");
            si.versionnum = 0x040100;
            si.numaudios = 1;
            si.openedaudio[0] = 0;
            si.nummixers = 1;
            si.numcards = 1;
            si.numaudioengines = 1;
            strcpy(si.license, "Cervus");
            for (int i = 0; i < 172; i++) si.filler[i] = -1;
            return put_user(arg, &si, sizeof si);
        }
        case X_MIX_NRMIX:
            return put_int(arg, 1);
        case X_AUDIOINFO:
        case X_AUDIOINFO_EX:
        case X_ENGINEINFO: {
            k_audioinfo_t ai;
            int dev;
            if (get_int(arg, &dev) < 0) return -EFAULT;
            if (dev != -1 && dev != 0) return -ENXIO;
            fill_audioinfo(&ai);
            return put_user(arg, &ai, sizeof ai);
        }
        case X_MIXERINFO: {
            k_mixerinfo_t mi;
            int dev;
            if (get_int(arg, &dev) < 0) return -EFAULT;
            if (dev != -1 && dev != 0) return -ENXIO;
            memset(&mi, 0, sizeof mi);
            strcpy(mi.id, "cervus");
            snprintf(mi.name, sizeof mi.name, "Cervus %s mixer", audio_backend_name());
            mi.enabled = 1;
            mi.priority = 10;
            strcpy(mi.devnode, "/dev/mixer");
            strcpy(mi.handle, "cervus-mixer0");
            for (int i = 0; i < 245; i++) mi.filler[i] = -1;
            return put_user(arg, &mi, sizeof mi);
        }
        case X_CARDINFO: {
            k_cardinfo_t ci;
            int card;
            if (get_int(arg, &card) < 0) return -EFAULT;
            if (card != -1 && card != 0) return -ENXIO;
            memset(&ci, 0, sizeof ci);
            strcpy(ci.shortname, audio_backend_name());
            snprintf(ci.longname, sizeof ci.longname, "Cervus %s audio", audio_backend_name());
            for (int i = 0; i < 154; i++) ci.filler[i] = -1;
            return put_user(arg, &ci, sizeof ci);
        }
        case OSS_GETVERSION:
            return put_int(arg, 0x040100);
        case MIX_INFO: {
            k_mixer_info_t m;
            memset(&m, 0, sizeof m);
            strcpy(m.id, "cervus");
            snprintf(m.name, sizeof m.name, "Cervus %s mixer", audio_backend_name());
            return put_user(arg, &m, sizeof m);
        }
        default:
            break;
    }
    uint32_t type = (uint32_t)(req >> 8) & 0xFF;
    uint32_t nr = (uint32_t)req & 0xFF;
    uint32_t dir = (uint32_t)(req >> 30) & 3;
    if (type == 'M') {
        audio_mixer_t mx;
        int vol = 0;
        if (audio_mixer_get(&mx) == 0) vol = mx.mute ? 0 : mx.volume;
        if (nr == 0xFE || nr == 0xFB) return put_int(arg, MIXER_DEVMASK);
        if (nr == 0xFD || nr == 0xFF || nr == 0xFC || nr == 0xF9 || nr == 0xFA) return put_int(arg, 0);
        if (nr == MIXER_VOLUME || nr == MIXER_PCM) {
            if (dir == 3) {
                int v;
                if (get_int(arg, &v) < 0) return -EFAULT;
                int l = v & 0xFF, r = (v >> 8) & 0xFF;
                int pct = l > r ? l : r;
                if (pct > 100) pct = 100;
                audio_set_volume(pct);
                vol = pct;
            }
            return put_int(arg, vol | (vol << 8));
        }
        if (nr < 32) return -EINVAL;
    }
    return -ENOTTY;
}

static int dsp_open(vnode_t *node, vfs_file_t *file) {
    (void)node;
    if ((file->flags & O_ACCMODE) == O_RDONLY) return -ENODEV;
    return pcm_open(file) ? 0 : -EBUSY;
}

static void dsp_release(vfs_file_t *file) {
    pcm_stream_t *s = pcm_find(file);
    if (s) pcm_close(s, (file->flags & O_NONBLOCK) ? 0 : 1);
}

static int64_t dsp_write_file(vfs_file_t *file, const void *buf, size_t len) {
    pcm_stream_t *s = pcm_find(file);
    if (!s) return -EBADF;
    return pcm_write(s, buf, len, vfs_io_nonblock());
}

static int64_t dsp_read(vnode_t *node, void *buf, size_t len, uint64_t off) {
    (void)node; (void)buf; (void)len; (void)off;
    return -EINVAL;
}

static int dsp_poll_file(vfs_file_t *file, int events) {
    pcm_stream_t *s = pcm_find(file);
    int r = 0;
    if (s) pcm_poll_wait(s);
    if (s && (events & POLLOUT) && pcm_writable(s)) r |= POLLOUT;
    return r;
}

static int64_t dsp_ioctl_file(vfs_file_t *file, uint64_t req, uint64_t arg) {
    pcm_stream_t *s = pcm_find(file);
    if (!s) return common_ioctl(req, arg);
    int v;
    switch (req) {
        case DSP_HALT:
        case DSP_HALT_OUTPUT:
            pcm_reset(s);
            return 0;
        case DSP_HALT_INPUT:
            return 0;
        case DSP_SYNC:
            return pcm_sync(s);
        case DSP_POST:
            pcm_post(s);
            return 0;
        case DSP_SPEED:
            if (get_int(arg, &v) < 0) return -EFAULT;
            if (v <= 0) return put_int(arg, (int)pcm_get_rate(s));
            return put_int(arg, (int)pcm_set_rate(s, (uint32_t)v));
        case PCM_READ_RATE:
            return put_int(arg, (int)pcm_get_rate(s));
        case DSP_STEREO:
            if (get_int(arg, &v) < 0) return -EFAULT;
            return put_int(arg, pcm_set_channels(s, v ? 2 : 1) - 1);
        case DSP_CHANNELS:
            if (get_int(arg, &v) < 0) return -EFAULT;
            if (v <= 0) return put_int(arg, pcm_get_channels(s));
            return put_int(arg, pcm_set_channels(s, v));
        case PCM_READ_CHANNELS:
            return put_int(arg, pcm_get_channels(s));
        case DSP_SETFMT:
            if (get_int(arg, &v) < 0) return -EFAULT;
            if (v == 0) return put_int(arg, pcm_get_format(s));
            return put_int(arg, pcm_set_format(s, v));
        case PCM_READ_BITS:
            return put_int(arg, pcm_get_format(s));
        case DSP_GETFMTS:
            return put_int(arg, PCM_FMTS);
        case DSP_GETBLKSIZE: {
            pcm_space_t sp;
            pcm_space(s, &sp);
            return put_int(arg, (int)sp.fragsize);
        }
        case DSP_SETFRAGMENT: {
            if (get_int(arg, &v) < 0) return -EFAULT;
            uint32_t shift = (uint32_t)v & 0xFFFF;
            uint32_t n = ((uint32_t)v >> 16) & 0x7FFF;
            if (shift < 4) shift = 4;
            if (shift > 16) shift = 16;
            if (n < 2) n = 2;
            pcm_set_fragment(s, 1u << shift, n == 0x7FFF ? 0 : n);
            return 0;
        }
        case DSP_SUBDIVIDE:
        case DSP_COOKEDMODE:
        case DSP_POLICY:
        case DSP_LOW_WATER:
        case DSP_SETSYNCRO:
        case DSP_SILENCE:
        case DSP_SKIP:
            return 0;
        case DSP_SETDUPLEX:
            return -EINVAL;
        case DSP_NONBLOCK:
            return 0;
        case DSP_GETCAPS:
            return put_int(arg, CAP_OUTPUT | CAP_TRIGGER | CAP_REALTIME | CAP_ANALOGOUT | 1);
        case DSP_GETTRIGGER:
            return put_int(arg, pcm_get_trigger(s) ? 2 : 0);
        case DSP_SETTRIGGER:
            if (get_int(arg, &v) < 0) return -EFAULT;
            pcm_set_trigger(s, v & 2);
            return 0;
        case DSP_GETOSPACE: {
            pcm_space_t sp;
            pcm_space(s, &sp);
            k_buf_info_t bi = { (int)sp.fragments, (int)sp.fragstotal, (int)sp.fragsize, (int)sp.bytes };
            return put_user(arg, &bi, sizeof bi);
        }
        case DSP_GETISPACE:
            return -EINVAL;
        case DSP_GETOPTR: {
            uint64_t played;
            uint32_t ptr, frags;
            pcm_position(s, &played, &ptr, &frags);
            k_count_info_t ci = { (unsigned int)played, (int)frags, (int)ptr };
            return put_user(arg, &ci, sizeof ci);
        }
        case DSP_GETIPTR: {
            k_count_info_t ci = { 0, 0, 0 };
            return put_user(arg, &ci, sizeof ci);
        }
        case DSP_CURRENT_OPTR: {
            uint64_t played;
            uint32_t ptr, frags;
            pcm_position(s, &played, &ptr, &frags);
            k_oss_count_t c;
            memset(&c, 0, sizeof c);
            int ch = pcm_get_channels(s);
            int fmt = pcm_get_format(s);
            int sb = fmt == PCM_FMT_U8 ? 1 : fmt == PCM_FMT_S16_LE ? 2 : 4;
            c.samples = (long long)(played / (uint64_t)(sb * ch));
            c.fifo_samples = (int)(pcm_delay(s) / (uint32_t)(sb * ch));
            return put_user(arg, &c, sizeof c);
        }
        case DSP_GETODELAY:
            return put_int(arg, (int)pcm_delay(s));
        case DSP_GETPLAYVOL:
            return put_int(arg, pcm_get_volume(s));
        case DSP_SETPLAYVOL:
            if (get_int(arg, &v) < 0) return -EFAULT;
            pcm_set_volume(s, v);
            return put_int(arg, pcm_get_volume(s));
        case DSP_GETERROR: {
            k_errinfo_t e;
            memset(&e, 0, sizeof e);
            e.play_underruns = (int)pcm_xruns(s);
            return put_user(arg, &e, sizeof e);
        }
        case DSP_GET_PLAYTGT:
            return put_int(arg, 0);
        case DSP_SET_PLAYTGT:
            return put_int(arg, 0);
        default:
            return common_ioctl(req, arg);
    }
}

static int64_t mixer_ioctl(vnode_t *node, uint64_t req, uint64_t arg) {
    (void)node;
    return common_ioctl(req, arg);
}

static int chr_stat(vnode_t *n, vfs_stat_t *out) {
    memset(out, 0, sizeof *out);
    out->st_ino   = n->ino;
    out->st_type  = VFS_NODE_CHARDEV;
    out->st_mode  = 0020000 | (n->mode & 0777);
    out->st_nlink = 1;
    out->st_rdev  = n->rdev;
    return 0;
}

static void chr_ref(vnode_t *n) { __atomic_fetch_add(&n->refcount, 1, __ATOMIC_RELAXED); }
static void chr_unref(vnode_t *n) { if (n->refcount > 1) __atomic_fetch_sub(&n->refcount, 1, __ATOMIC_RELAXED); }

static int64_t sndstat_read(vnode_t *node, void *buf, size_t len, uint64_t off) {
    (void)node;
    char tmp[1536];
    int n = snprintf(tmp, sizeof tmp,
                     "Cervus OSS 4.1 compatible audio\n\nAudio devices:\n0: Cervus %s (%d streams)\n\nMixers:\n0: Cervus %s mixer\n\n",
                     audio_backend_name(), pcm_streams_open(), audio_backend_name());
    if (n < 0) return 0;
    if ((size_t)n < sizeof tmp) n += pcm_status(tmp + n, sizeof tmp - (size_t)n);
    if (n > (int)sizeof tmp) n = (int)sizeof tmp;
    if (off >= (uint64_t)n) return 0;
    size_t k = (size_t)n - (size_t)off;
    if (k > len) k = len;
    memcpy(buf, tmp + off, k);
    return (int64_t)k;
}

static const vnode_ops_t DSP_OPS = {
    .read         = dsp_read,
    .write_file   = dsp_write_file,
    .poll_file    = dsp_poll_file,
    .ioctl_file   = dsp_ioctl_file,
    .stat         = chr_stat,
    .ref          = chr_ref,
    .unref        = chr_unref,
    .open_dev     = dsp_open,
    .release_file = dsp_release,
};

static const vnode_ops_t MIXER_OPS = {
    .ioctl_raw = mixer_ioctl,
    .stat      = chr_stat,
    .ref       = chr_ref,
    .unref     = chr_unref,
};

static const vnode_ops_t SNDSTAT_OPS = {
    .read  = sndstat_read,
    .stat  = chr_stat,
    .ref   = chr_ref,
    .unref = chr_unref,
};

static vnode_t g_dsp_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0666, .ops = &DSP_OPS, .refcount = 1, .ino = 903,
};

static vnode_t g_mixer_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0666, .ops = &MIXER_OPS, .refcount = 1, .ino = 900,
};

static vnode_t g_sndstat_node = {
    .type = VFS_NODE_CHARDEV, .mode = 0444, .ops = &SNDSTAT_OPS, .refcount = 1, .ino = 906,
};

void oss_init(void) {
    g_dsp_node.rdev = vfs_makedev(14, 3);
    g_mixer_node.rdev = vfs_makedev(14, 0);
    g_sndstat_node.rdev = vfs_makedev(14, 6);
    devfs_register("dsp", &g_dsp_node);
    devfs_register("dsp0", &g_dsp_node);
    devfs_register("mixer", &g_mixer_node);
    devfs_register("mixer0", &g_mixer_node);
    devfs_register("sndstat", &g_sndstat_node);
}
