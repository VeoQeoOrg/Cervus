#include "../../../include/drm/drm_uapi.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/fs/devfs.h"
#include "../../../include/fs/poll.h"
#include "../../../include/graphics/fb/fb.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/sched/sched.h"
#include "../../../include/sched/spinlock.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/io/serial.h"
#include "../../../include/time/clocksource.h"
#include "../../../include/syscall/syscall_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DRM_MAX_DUMB   16
#define DRM_MAX_FBS    16
#define DRM_EVENT_QUEUE 16

#define ID_CRTC      1
#define ID_CONNECTOR 2
#define ID_ENCODER   3
#define ID_PLANE     4

#define PROP_ACTIVE      10
#define PROP_MODE_ID     11
#define PROP_CRTC_ID     12
#define PROP_FB_ID       13
#define PROP_CRTC_X      14
#define PROP_CRTC_Y      15
#define PROP_CRTC_W      16
#define PROP_CRTC_H      17
#define PROP_SRC_X       18
#define PROP_SRC_Y       19
#define PROP_SRC_W       20
#define PROP_SRC_H       21
#define PROP_TYPE        22
#define PROP_DPMS        23
#define PROP_IN_FORMATS  24

#define PLANE_TYPE_PRIMARY 1

#define DRM_MAX_BLOBS 8

#define DUMB_OFFSET_BASE 0x100000000ULL
#define DUMB_OFFSET_STEP 0x10000000ULL

typedef struct {
    int      used;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size;
    size_t   npages;
    void   **pages;
} dumb_t;

typedef struct {
    int      used;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} drm_fb_t;

typedef struct {
    int      valid;
    uint32_t fb_id;
    uint32_t mode_blob;
    struct drm_mode_modeinfo mode;
} drm_crtc_t;

extern fb_info_t *global_framebuffer;
extern void vt_fb_acquire(int vt);
extern void vt_fb_release(int vt);
extern void vt_fb_set_owner_task(task_t *t);
extern int  vt_fb_may_draw(int vt);
extern void console_force_full_redraw(void);

static dumb_t     g_dumb[DRM_MAX_DUMB];
static drm_fb_t   g_fbs[DRM_MAX_FBS];
static drm_crtc_t g_crtc;
static uint32_t   g_next_handle = 1;
static uint32_t   g_next_fb_id  = 1;
static spinlock_t g_drm_lock = SPINLOCK_INIT;
static vnode_t    g_card_node;
static int        g_master;
static int        g_scanout_on;

typedef struct {
    int      used;
    uint32_t id;
    uint32_t length;
    uint8_t *data;
} drm_blob_t;

static drm_blob_t g_blobs[DRM_MAX_BLOBS];
static uint32_t   g_next_blob_id = 1;

static struct drm_event_vblank g_events[DRM_EVENT_QUEUE];
static int g_ev_head, g_ev_tail, g_ev_count;
static task_t *g_ev_waiter;

static uint32_t fb_width(void)  { return global_framebuffer ? global_framebuffer->width  : 0; }
static uint32_t fb_height(void) { return global_framebuffer ? global_framebuffer->height : 0; }

static void fill_native_mode(struct drm_mode_modeinfo *m)
{
    uint32_t w = fb_width(), h = fb_height();
    memset(m, 0, sizeof *m);
    m->clock       = (w * h * 60u) / 1000u;
    m->hdisplay    = (uint16_t)w;
    m->hsync_start = (uint16_t)(w + 16);
    m->hsync_end   = (uint16_t)(w + 32);
    m->htotal      = (uint16_t)(w + 64);
    m->vdisplay    = (uint16_t)h;
    m->vsync_start = (uint16_t)(h + 2);
    m->vsync_end   = (uint16_t)(h + 6);
    m->vtotal      = (uint16_t)(h + 10);
    m->vrefresh    = 60;
    m->type        = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
    snprintf(m->name, sizeof m->name, "%ux%u", w, h);
}

static dumb_t *dumb_by_handle(uint32_t handle)
{
    for (int i = 0; i < DRM_MAX_DUMB; i++)
        if (g_dumb[i].used && g_dumb[i].handle == handle) return &g_dumb[i];
    return NULL;
}

static drm_fb_t *fb_by_id(uint32_t id)
{
    for (int i = 0; i < DRM_MAX_FBS; i++)
        if (g_fbs[i].used && g_fbs[i].fb_id == id) return &g_fbs[i];
    return NULL;
}

static void dumb_free(dumb_t *d)
{
    if (!d->pages) return;
    for (size_t i = 0; i < d->npages; i++)
        if (d->pages[i]) pmm_free(d->pages[i], 1);
    free(d->pages);
    d->pages = NULL;
    d->npages = 0;
    d->used = 0;
}

static int scanout(uint32_t fb_id)
{
    drm_fb_t *f = fb_by_id(fb_id);
    if (!f) return -ENOENT;
    dumb_t *d = dumb_by_handle(f->handle);
    if (!d) return -ENOENT;
    fb_info_t *dst = global_framebuffer;
    if (!dst) return -ENODEV;

    if (!g_scanout_on) {
        task_t *t = syscall_cur_task();
        vt_fb_acquire(t ? t->ctty : 0);
        vt_fb_set_owner_task(t);
        g_scanout_on = 1;
    }

    uint32_t rows = f->height < (uint32_t)dst->height ? f->height : (uint32_t)dst->height;
    uint32_t cols = f->width  < (uint32_t)dst->width  ? f->width  : (uint32_t)dst->width;
    uint32_t bytes = cols * 4;

    uint32_t *bb     = fb_get_backbuffer();
    uint32_t  bpitch = bb ? fb_backbuffer_pitch() : (uint32_t)(dst->pitch / 4);
    uint32_t *base   = bb ? bb : (uint32_t *)dst->address;

    for (uint32_t y = 0; y < rows; y++) {
        uint8_t *drow = (uint8_t *)(base + (size_t)y * bpitch);
        uint64_t off  = (uint64_t)y * f->pitch;
        uint32_t done = 0;
        while (done < bytes) {
            size_t   page = (size_t)((off + done) >> 12);
            uint32_t in   = (uint32_t)((off + done) & 0xFFF);
            uint32_t n    = 0x1000 - in;
            if (n > bytes - done) n = bytes - done;
            if (page < d->npages && d->pages[page])
                memcpy(drow + done, (uint8_t *)d->pages[page] + in, n);
            done += n;
        }
    }

    if (bb) fb_flush_lines(dst, 0, rows);
    return 0;
}

void drm_forget_scanout(void)
{
    g_scanout_on = 0;
    g_crtc.valid = 0;
    g_crtc.fb_id = 0;
}

void drm_stop_scanout(void)
{
    if (!g_scanout_on) return;
    task_t *t = syscall_cur_task();
    int vt = t ? t->ctty : 0;
    vt_fb_release(vt);
    g_scanout_on = 0;
    g_crtc.valid = 0;
    g_crtc.fb_id = 0;
    if (vt_fb_may_draw(vt)) console_force_full_redraw();
}

static void queue_flip_event(uint32_t crtc_id, uint64_t user_data)
{
    uint64_t f = spinlock_acquire_irqsave(&g_drm_lock);
    if (g_ev_count < DRM_EVENT_QUEUE) {
        struct drm_event_vblank *e = &g_events[g_ev_head];
        memset(e, 0, sizeof *e);
        e->base.type   = DRM_EVENT_FLIP_COMPLETE;
        e->base.length = sizeof *e;
        e->user_data   = user_data;
        uint64_t now   = clocksource_now_ns();
        e->tv_sec      = (uint32_t)(now / 1000000000ULL);
        e->tv_usec     = (uint32_t)((now % 1000000000ULL) / 1000ULL);
        e->sequence    = 0;
        e->crtc_id     = crtc_id;
        g_ev_head = (g_ev_head + 1) % DRM_EVENT_QUEUE;
        g_ev_count++;
    }
    task_t *w = g_ev_waiter;
    g_ev_waiter = NULL;
    spinlock_release_irqrestore(&g_drm_lock, f);
    if (w) task_unblock(w);
}

static int64_t drm_read(vnode_t *n, void *buf, size_t len, uint64_t off)
{
    (void)n; (void)off;
    if (len < sizeof(struct drm_event_vblank)) return -EINVAL;

    uint64_t f = spinlock_acquire_irqsave(&g_drm_lock);
    if (g_ev_count == 0) { spinlock_release_irqrestore(&g_drm_lock, f); return -EAGAIN; }
    struct drm_event_vblank *e = &g_events[g_ev_tail];
    memcpy(buf, e, sizeof *e);
    g_ev_tail = (g_ev_tail + 1) % DRM_EVENT_QUEUE;
    g_ev_count--;
    spinlock_release_irqrestore(&g_drm_lock, f);
    return (int64_t)sizeof(struct drm_event_vblank);
}

static int drm_poll(vnode_t *n, int events)
{
    (void)n; (void)events;
    int r = POLLOUT;
    uint64_t f = spinlock_acquire_irqsave(&g_drm_lock);
    if (g_ev_count > 0) r |= POLLIN;
    spinlock_release_irqrestore(&g_drm_lock, f);
    return r;
}

static int drm_mmap_page(vnode_t *n, uint64_t offset, uintptr_t *phys_out)
{
    (void)n;
    if (offset < DUMB_OFFSET_BASE) return -1;
    uint64_t rel   = offset - DUMB_OFFSET_BASE;
    uint32_t slot  = (uint32_t)(rel / DUMB_OFFSET_STEP);
    uint64_t inbuf = rel % DUMB_OFFSET_STEP;
    if (slot >= DRM_MAX_DUMB) return -1;

    dumb_t *d = &g_dumb[slot];
    if (!d->used) return -1;
    size_t page = (size_t)(inbuf >> 12);
    if (page >= d->npages || !d->pages[page]) return -1;
    *phys_out = pmm_virt_to_phys(d->pages[page]);
    return 0;
}

static int copy_str_out(uint64_t uptr, uint64_t *ulen, const char *s)
{
    size_t n = strlen(s);
    if (uptr && *ulen >= n)
        if (syscall_copy_to_user((void *)uptr, (void *)s, n) < 0) return -EFAULT;
    *ulen = n;
    return 0;
}

static int64_t ioctl_version(struct drm_version *v)
{
    v->version_major      = 1;
    v->version_minor      = 0;
    v->version_patchlevel = 0;
    if (copy_str_out(v->name, &v->name_len, "cervus") < 0) return -EFAULT;
    if (copy_str_out(v->date, &v->date_len, "20260922") < 0) return -EFAULT;
    if (copy_str_out(v->desc, &v->desc_len, "Cervus framebuffer KMS") < 0) return -EFAULT;
    return 0;
}

static int64_t ioctl_get_cap(struct drm_get_cap *c)
{
    switch (c->capability) {
        case DRM_CAP_DUMB_BUFFER:          c->value = 1; break;
        case DRM_CAP_DUMB_PREFERRED_DEPTH: c->value = 24; break;
        case DRM_CAP_DUMB_PREFER_SHADOW:   c->value = 1; break;
        case DRM_CAP_TIMESTAMP_MONOTONIC:  c->value = 1; break;
        case DRM_CAP_CRTC_IN_VBLANK_EVENT: c->value = 1; break;
        case DRM_CAP_CURSOR_WIDTH:
        case DRM_CAP_CURSOR_HEIGHT:        c->value = 64; break;
        default:                           c->value = 0; break;
    }
    return 0;
}

static int put_u32_array(uint64_t uptr, uint32_t *count, const uint32_t *vals, uint32_t have)
{
    if (uptr && *count >= have) {
        if (have && syscall_copy_to_user((void *)uptr, (void *)vals,
                                         have * sizeof(uint32_t)) < 0) return -EFAULT;
    }
    *count = have;
    return 0;
}

static int64_t ioctl_getresources(struct drm_mode_card_res *r)
{
    uint32_t crtc = ID_CRTC, conn = ID_CONNECTOR, enc = ID_ENCODER;

    uint32_t fb_ids[DRM_MAX_FBS];
    uint32_t nfb = 0;
    for (int i = 0; i < DRM_MAX_FBS; i++)
        if (g_fbs[i].used) fb_ids[nfb++] = g_fbs[i].fb_id;

    if (put_u32_array(r->fb_id_ptr, &r->count_fbs, fb_ids, nfb) < 0) return -EFAULT;
    if (put_u32_array(r->crtc_id_ptr, &r->count_crtcs, &crtc, 1) < 0) return -EFAULT;
    if (put_u32_array(r->connector_id_ptr, &r->count_connectors, &conn, 1) < 0) return -EFAULT;
    if (put_u32_array(r->encoder_id_ptr, &r->count_encoders, &enc, 1) < 0) return -EFAULT;

    r->min_width  = 64;
    r->max_width  = fb_width();
    r->min_height = 64;
    r->max_height = fb_height();
    return 0;
}

static int64_t ioctl_getcrtc(struct drm_mode_crtc *c)
{
    if (c->crtc_id != ID_CRTC) return -ENOENT;
    c->count_connectors = 0;
    c->set_connectors_ptr = 0;
    c->fb_id      = g_crtc.valid ? g_crtc.fb_id : 0;
    c->x          = 0;
    c->y          = 0;
    c->gamma_size = 0;
    c->mode_valid = g_crtc.valid;
    if (g_crtc.valid) c->mode = g_crtc.mode;
    else              memset(&c->mode, 0, sizeof c->mode);
    return 0;
}

static int64_t ioctl_setcrtc(struct drm_mode_crtc *c)
{
    if (c->crtc_id != ID_CRTC) return -ENOENT;

    if (!c->mode_valid || c->fb_id == 0) {
        g_crtc.valid = 0;
        g_crtc.fb_id = 0;
        drm_stop_scanout();
        return 0;
    }

    if (c->mode.hdisplay != fb_width() || c->mode.vdisplay != fb_height())
        return -EINVAL;
    if (!fb_by_id(c->fb_id)) return -ENOENT;

    g_crtc.valid = 1;
    g_crtc.fb_id = c->fb_id;
    g_crtc.mode  = c->mode;
    return scanout(c->fb_id);
}

static int64_t ioctl_getencoder(struct drm_mode_get_encoder *e)
{
    if (e->encoder_id != ID_ENCODER) return -ENOENT;
    e->encoder_type    = DRM_MODE_ENCODER_VIRTUAL;
    e->crtc_id         = ID_CRTC;
    e->possible_crtcs  = 1;
    e->possible_clones = 0;
    return 0;
}

static int64_t ioctl_getconnector(struct drm_mode_get_connector *c)
{
    if (c->connector_id != ID_CONNECTOR) return -ENOENT;

    uint32_t enc = ID_ENCODER;
    if (put_u32_array(c->encoders_ptr, &c->count_encoders, &enc, 1) < 0) return -EFAULT;

    struct drm_mode_modeinfo mode;
    fill_native_mode(&mode);
    if (c->modes_ptr && c->count_modes >= 1) {
        if (syscall_copy_to_user((void *)c->modes_ptr, &mode, sizeof mode) < 0) return -EFAULT;
    }
    c->count_modes = 1;
    c->count_props = 0;

    c->encoder_id         = ID_ENCODER;
    c->connector_type     = DRM_MODE_CONNECTOR_VIRTUAL;
    c->connector_type_id  = 1;
    c->connection         = DRM_MODE_CONNECTED;
    c->mm_width           = fb_width()  / 4;
    c->mm_height          = fb_height() / 4;
    c->subpixel           = DRM_MODE_SUBPIXEL_UNKNOWN;
    return 0;
}

static int64_t ioctl_create_dumb(struct drm_mode_create_dumb *d)
{
    if (d->bpp != 32 || !d->width || !d->height) return -EINVAL;
    if (d->width > 8192 || d->height > 8192) return -EINVAL;

    int slot = -1;
    for (int i = 0; i < DRM_MAX_DUMB; i++)
        if (!g_dumb[i].used) { slot = i; break; }
    if (slot < 0) return -ENOSPC;

    uint32_t pitch = d->width * 4;
    uint64_t size  = (uint64_t)pitch * d->height;
    size_t npages  = (size_t)((size + 0xFFF) >> 12);

    dumb_t *b = &g_dumb[slot];
    b->pages = calloc(npages, sizeof(void *));
    if (!b->pages) return -ENOMEM;
    for (size_t i = 0; i < npages; i++) {
        b->pages[i] = pmm_alloc_zero(1);
        if (!b->pages[i]) {
            for (size_t j = 0; j < i; j++) pmm_free(b->pages[j], 1);
            free(b->pages);
            b->pages = NULL;
            return -ENOMEM;
        }
    }

    b->used   = 1;
    b->handle = g_next_handle++;
    b->width  = d->width;
    b->height = d->height;
    b->pitch  = pitch;
    b->size   = size;
    b->npages = npages;

    d->handle = b->handle;
    d->pitch  = pitch;
    d->size   = size;
    return 0;
}

static int64_t ioctl_map_dumb(struct drm_mode_map_dumb *m)
{
    for (int i = 0; i < DRM_MAX_DUMB; i++) {
        if (g_dumb[i].used && g_dumb[i].handle == m->handle) {
            m->offset = DUMB_OFFSET_BASE + (uint64_t)i * DUMB_OFFSET_STEP;
            return 0;
        }
    }
    return -ENOENT;
}

static int64_t ioctl_destroy_dumb(struct drm_mode_destroy_dumb *d)
{
    dumb_t *b = dumb_by_handle(d->handle);
    if (!b) return -ENOENT;
    for (int i = 0; i < DRM_MAX_FBS; i++)
        if (g_fbs[i].used && g_fbs[i].handle == d->handle) g_fbs[i].used = 0;
    dumb_free(b);
    return 0;
}

static int64_t add_fb(uint32_t handle, uint32_t w, uint32_t h, uint32_t pitch, uint32_t *out_id)
{
    if (!dumb_by_handle(handle)) return -ENOENT;
    int slot = -1;
    for (int i = 0; i < DRM_MAX_FBS; i++)
        if (!g_fbs[i].used) { slot = i; break; }
    if (slot < 0) return -ENOSPC;

    drm_fb_t *f = &g_fbs[slot];
    f->used   = 1;
    f->fb_id  = g_next_fb_id++;
    f->handle = handle;
    f->width  = w;
    f->height = h;
    f->pitch  = pitch;
    *out_id = f->fb_id;
    return 0;
}

static int64_t ioctl_addfb(struct drm_mode_fb_cmd *c)
{
    if (c->bpp != 32) return -EINVAL;
    return add_fb(c->handle, c->width, c->height, c->pitch, &c->fb_id);
}

static int64_t ioctl_addfb2(struct drm_mode_fb_cmd2 *c)
{
    if (c->pixel_format != DRM_FORMAT_XRGB8888 &&
        c->pixel_format != DRM_FORMAT_ARGB8888) return -EINVAL;
    return add_fb(c->handles[0], c->width, c->height, c->pitches[0], &c->fb_id);
}

static int64_t ioctl_rmfb(uint32_t *id)
{
    drm_fb_t *f = fb_by_id(*id);
    if (!f) return -ENOENT;
    if (g_crtc.valid && g_crtc.fb_id == *id) { g_crtc.valid = 0; g_crtc.fb_id = 0; }
    f->used = 0;
    return 0;
}

static int64_t ioctl_page_flip(struct drm_mode_crtc_page_flip *p)
{
    if (p->crtc_id != ID_CRTC) return -ENOENT;
    if (!fb_by_id(p->fb_id)) return -ENOENT;

    int r = scanout(p->fb_id);
    if (r < 0) return r;
    g_crtc.fb_id = p->fb_id;

    if (p->flags & DRM_MODE_PAGE_FLIP_EVENT)
        queue_flip_event(p->crtc_id, p->user_data);
    return 0;
}

static int64_t ioctl_getplaneres(struct drm_mode_get_plane_res *r)
{
    uint32_t plane = ID_PLANE;
    if (put_u32_array(r->plane_id_ptr, &r->count_planes, &plane, 1) < 0) return -EFAULT;
    return 0;
}

static int64_t ioctl_getplane(struct drm_mode_get_plane *p)
{
    if (p->plane_id != ID_PLANE) return -ENOENT;
    static const uint32_t formats[] = { DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888 };
    p->crtc_id        = g_crtc.valid ? ID_CRTC : 0;
    p->fb_id          = g_crtc.valid ? g_crtc.fb_id : 0;
    p->possible_crtcs = 1;
    p->gamma_size     = 0;
    if (p->format_type_ptr && p->count_format_types >= 2) {
        if (syscall_copy_to_user((void *)p->format_type_ptr, (void *)formats,
                                 sizeof formats) < 0) return -EFAULT;
    }
    p->count_format_types = 2;
    return 0;
}



static drm_blob_t *blob_by_id(uint32_t id)
{
    for (int i = 0; i < DRM_MAX_BLOBS; i++)
        if (g_blobs[i].used && g_blobs[i].id == id) return &g_blobs[i];
    return NULL;
}

static int64_t ioctl_create_blob(struct drm_mode_create_blob *c)
{
    if (!c->length || c->length > 4096) return -EINVAL;
    int slot = -1;
    for (int i = 0; i < DRM_MAX_BLOBS; i++)
        if (!g_blobs[i].used) { slot = i; break; }
    if (slot < 0) return -ENOSPC;

    uint8_t *buf = malloc(c->length);
    if (!buf) return -ENOMEM;
    if (syscall_copy_from_user(buf, (const void *)c->data, c->length) < 0) {
        free(buf);
        return -EFAULT;
    }
    g_blobs[slot].used   = 1;
    g_blobs[slot].id     = g_next_blob_id++;
    g_blobs[slot].length = c->length;
    g_blobs[slot].data   = buf;
    c->blob_id = g_blobs[slot].id;
    return 0;
}

static int64_t ioctl_destroy_blob(struct drm_mode_destroy_blob *d)
{
    drm_blob_t *b = blob_by_id(d->blob_id);
    if (!b) return -ENOENT;
    free(b->data);
    b->data = NULL;
    b->used = 0;
    return 0;
}

static int64_t ioctl_getblob(struct drm_mode_get_blob *g)
{
    drm_blob_t *b = blob_by_id(g->blob_id);
    if (!b) return -ENOENT;
    if (g->data && g->length >= b->length) {
        if (syscall_copy_to_user((void *)g->data, b->data, b->length) < 0) return -EFAULT;
    }
    g->length = b->length;
    return 0;
}

typedef struct {
    uint32_t           id;
    const char        *name;
    uint32_t           flags;
    int64_t            min;
    int64_t            max;
    const char *const *enums;
    uint32_t           nenums;
    uint32_t           obj_type;
} drm_prop_desc_t;

static const char *const PLANE_TYPE_NAMES[] = { "Overlay", "Primary", "Cursor" };
static const char *const DPMS_NAMES[]       = { "On", "Standby", "Suspend", "Off" };

#define P_RANGE(i, n, lo, hi) { .id = (i), .name = (n), .flags = DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC, .min = (lo), .max = (hi) }
#define P_SRANGE(i, n)        { .id = (i), .name = (n), .flags = DRM_MODE_PROP_SIGNED_RANGE | DRM_MODE_PROP_ATOMIC, .min = INT32_MIN, .max = INT32_MAX }
#define P_OBJ(i, n, t)        { .id = (i), .name = (n), .flags = DRM_MODE_PROP_OBJECT | DRM_MODE_PROP_ATOMIC, .obj_type = (t) }

static const drm_prop_desc_t CRTC_PROPS[] = {
    P_RANGE(PROP_ACTIVE, "ACTIVE", 0, 1),
    { .id = PROP_MODE_ID, .name = "MODE_ID", .flags = DRM_MODE_PROP_BLOB | DRM_MODE_PROP_ATOMIC },
};

static const drm_prop_desc_t CONN_PROPS[] = {
    P_OBJ(PROP_CRTC_ID, "CRTC_ID", DRM_MODE_OBJECT_CRTC),
    { .id = PROP_DPMS, .name = "DPMS", .flags = DRM_MODE_PROP_ENUM, .enums = DPMS_NAMES, .nenums = 4 },
};

static const drm_prop_desc_t PLANE_PROPS[] = {
    { .id = PROP_TYPE, .name = "type", .flags = DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE,
      .enums = PLANE_TYPE_NAMES, .nenums = 3 },
    P_OBJ(PROP_FB_ID,   "FB_ID",   DRM_MODE_OBJECT_FB),
    P_OBJ(PROP_CRTC_ID, "CRTC_ID", DRM_MODE_OBJECT_CRTC),
    P_SRANGE(PROP_CRTC_X, "CRTC_X"),
    P_SRANGE(PROP_CRTC_Y, "CRTC_Y"),
    P_RANGE(PROP_CRTC_W, "CRTC_W", 0, INT32_MAX),
    P_RANGE(PROP_CRTC_H, "CRTC_H", 0, INT32_MAX),
    P_RANGE(PROP_SRC_X,  "SRC_X",  0, UINT32_MAX),
    P_RANGE(PROP_SRC_Y,  "SRC_Y",  0, UINT32_MAX),
    P_RANGE(PROP_SRC_W,  "SRC_W",  0, UINT32_MAX),
    P_RANGE(PROP_SRC_H,  "SRC_H",  0, UINT32_MAX),
};

static const drm_prop_desc_t *props_of(uint32_t obj_id, int *count)
{
    switch (obj_id) {
        case ID_CRTC:      *count = (int)(sizeof CRTC_PROPS  / sizeof CRTC_PROPS[0]);  return CRTC_PROPS;
        case ID_CONNECTOR: *count = (int)(sizeof CONN_PROPS  / sizeof CONN_PROPS[0]);  return CONN_PROPS;
        case ID_PLANE:     *count = (int)(sizeof PLANE_PROPS / sizeof PLANE_PROPS[0]); return PLANE_PROPS;
        default:           *count = 0;                                                 return NULL;
    }
}

static uint64_t prop_value(uint32_t obj_id, uint32_t prop_id)
{
    if (obj_id == ID_CRTC) {
        if (prop_id == PROP_ACTIVE)  return g_crtc.valid ? 1 : 0;
        if (prop_id == PROP_MODE_ID) return g_crtc.mode_blob;
    }
    if (obj_id == ID_CONNECTOR && prop_id == PROP_CRTC_ID)
        return g_crtc.valid ? ID_CRTC : 0;
    if (obj_id == ID_PLANE) {
        switch (prop_id) {
            case PROP_TYPE:    return PLANE_TYPE_PRIMARY;
            case PROP_FB_ID:   return g_crtc.valid ? g_crtc.fb_id : 0;
            case PROP_CRTC_ID: return g_crtc.valid ? ID_CRTC : 0;
            case PROP_CRTC_W:  return fb_width();
            case PROP_CRTC_H:  return fb_height();
            case PROP_SRC_W:   return (uint64_t)fb_width()  << 16;
            case PROP_SRC_H:   return (uint64_t)fb_height() << 16;
            default:           return 0;
        }
    }
    return 0;
}

static int64_t ioctl_getprop(struct drm_mode_get_property *g)
{
    const uint32_t objs[] = { ID_CRTC, ID_CONNECTOR, ID_PLANE };
    const drm_prop_desc_t *d = NULL;
    for (unsigned o = 0; o < sizeof objs / sizeof objs[0] && !d; o++) {
        int n = 0;
        const drm_prop_desc_t *list = props_of(objs[o], &n);
        for (int i = 0; i < n; i++)
            if (list[i].id == g->prop_id) { d = &list[i]; break; }
    }
    if (!d) return -ENOENT;

    uint64_t values[8];
    uint32_t nvalues = 0, nenums = 0;
    if (d->flags & (DRM_MODE_PROP_RANGE | DRM_MODE_PROP_SIGNED_RANGE)) {
        values[0] = (uint64_t)d->min;
        values[1] = (uint64_t)d->max;
        nvalues = 2;
    } else if (d->flags & DRM_MODE_PROP_ENUM) {
        for (uint32_t i = 0; i < d->nenums && i < 8; i++) values[i] = i;
        nvalues = nenums = d->nenums;
    } else if (d->flags & DRM_MODE_PROP_OBJECT) {
        values[0] = d->obj_type;
        nvalues = 1;
    }

    if (nvalues && g->values_ptr && g->count_values >= nvalues) {
        if (syscall_copy_to_user((void *)g->values_ptr, values, nvalues * sizeof values[0]) < 0)
            return -EFAULT;
    }
    if (nenums && g->enum_blob_ptr && g->count_enum_blobs >= nenums) {
        for (uint32_t i = 0; i < nenums; i++) {
            struct drm_mode_property_enum e;
            memset(&e, 0, sizeof e);
            e.value = i;
            strncpy(e.name, d->enums[i], sizeof e.name - 1);
            if (syscall_copy_to_user((void *)(g->enum_blob_ptr + i * sizeof e), &e, sizeof e) < 0)
                return -EFAULT;
        }
    }

    g->flags = d->flags;
    memset(g->name, 0, sizeof g->name);
    strncpy(g->name, d->name, sizeof g->name - 1);
    g->count_values     = nvalues;
    g->count_enum_blobs = nenums;
    return 0;
}

static int64_t apply_prop(uint32_t obj_id, uint32_t prop_id, uint64_t value,
                          uint32_t *want_fb, int *want_active, int *saw_mode)
{
    if (obj_id == ID_CRTC) {
        if (prop_id == PROP_ACTIVE) { *want_active = value ? 1 : 0; return 0; }
        if (prop_id == PROP_MODE_ID) {
            if (value == 0) { *want_active = 0; return 0; }
            drm_blob_t *b = blob_by_id((uint32_t)value);
            if (!b || b->length < sizeof(struct drm_mode_modeinfo)) return -EINVAL;
            struct drm_mode_modeinfo m;
            memcpy(&m, b->data, sizeof m);
            if (m.hdisplay != fb_width() || m.vdisplay != fb_height()) return -EINVAL;
            g_crtc.mode      = m;
            g_crtc.mode_blob = (uint32_t)value;
            *saw_mode = 1;
            return 0;
        }
        return 0;
    }
    if (obj_id == ID_CONNECTOR) return 0;
    if (obj_id == ID_PLANE) {
        if (prop_id == PROP_FB_ID) { *want_fb = (uint32_t)value; return 0; }
        return 0;
    }
    return -ENOENT;
}

static int64_t ioctl_obj_getprops(struct drm_mode_obj_get_properties *o)
{
    int n = 0;
    const drm_prop_desc_t *list = props_of(o->obj_id, &n);
    if (!list) { o->count_props = 0; return 0; }

    if (o->props_ptr && o->prop_values_ptr && (int)o->count_props >= n) {
        uint32_t ids[16];
        uint64_t vals[16];
        for (int i = 0; i < n; i++) {
            ids[i]  = list[i].id;
            vals[i] = prop_value(o->obj_id, list[i].id);
        }
        if (syscall_copy_to_user((void *)o->props_ptr, ids,
                                 (size_t)n * sizeof(uint32_t)) < 0) return -EFAULT;
        if (syscall_copy_to_user((void *)o->prop_values_ptr, vals,
                                 (size_t)n * sizeof(uint64_t)) < 0) return -EFAULT;
    }
    o->count_props = (uint32_t)n;
    return 0;
}

static int64_t ioctl_atomic(struct drm_mode_atomic *a)
{
    if (a->flags & ~(DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_NONBLOCK |
                     DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT))
        return -EINVAL;
    if (a->count_objs == 0 || a->count_objs > 8) return -EINVAL;

    uint32_t objs[8], nprops[8];
    if (syscall_copy_from_user(objs, (const void *)a->objs_ptr,
                               a->count_objs * sizeof(uint32_t)) < 0) return -EFAULT;
    if (syscall_copy_from_user(nprops, (const void *)a->count_props_ptr,
                               a->count_objs * sizeof(uint32_t)) < 0) return -EFAULT;

    uint32_t total = 0;
    for (uint32_t i = 0; i < a->count_objs; i++) total += nprops[i];
    if (total == 0 || total > 64) return -EINVAL;

    uint32_t pids[64];
    uint64_t pvals[64];
    if (syscall_copy_from_user(pids, (const void *)a->props_ptr,
                               total * sizeof(uint32_t)) < 0) return -EFAULT;
    if (syscall_copy_from_user(pvals, (const void *)a->prop_values_ptr,
                               total * sizeof(uint64_t)) < 0) return -EFAULT;

    uint32_t want_fb = g_crtc.valid ? g_crtc.fb_id : 0;
    int want_active  = g_crtc.valid;
    int saw_mode     = 0;

    struct drm_mode_modeinfo saved_mode = g_crtc.mode;
    uint32_t saved_blob = g_crtc.mode_blob;

    uint32_t k = 0;
    for (uint32_t i = 0; i < a->count_objs; i++) {
        for (uint32_t j = 0; j < nprops[i]; j++, k++) {
            int64_t r = apply_prop(objs[i], pids[k], pvals[k],
                                   &want_fb, &want_active, &saw_mode);
            if (r < 0) {
                g_crtc.mode = saved_mode;
                g_crtc.mode_blob = saved_blob;
                return r;
            }
        }
    }

    if (a->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
        g_crtc.mode = saved_mode;
        g_crtc.mode_blob = saved_blob;
        return 0;
    }

    if (!want_active || want_fb == 0) {
        drm_stop_scanout();
        return 0;
    }

    if (!fb_by_id(want_fb)) return -ENOENT;
    if (!saw_mode && !g_crtc.valid) return -EINVAL;

    int r = scanout(want_fb);
    if (r < 0) return r;
    g_crtc.valid = 1;
    g_crtc.fb_id = want_fb;

    if (a->flags & DRM_MODE_PAGE_FLIP_EVENT)
        queue_flip_event(ID_CRTC, a->user_data);
    return 0;
}

static int64_t drm_ioctl(vnode_t *n, uint64_t req, void *arg)
{
    (void)n;
    if (!arg && req != DRM_IOCTL_SET_MASTER && req != DRM_IOCTL_DROP_MASTER)
        return -EINVAL;

    switch ((uint32_t)req) {
        case DRM_IOCTL_VERSION:        return ioctl_version(arg);
        case DRM_IOCTL_GET_CAP:        return ioctl_get_cap(arg);
        case DRM_IOCTL_SET_CLIENT_CAP: {
            struct drm_set_client_cap *c = arg;
            if (c->capability == DRM_CLIENT_CAP_UNIVERSAL_PLANES) return 0;
            if (c->capability == DRM_CLIENT_CAP_ASPECT_RATIO)     return 0;
            if (c->capability == DRM_CLIENT_CAP_ATOMIC)           return 0;
            return -EINVAL;
        }
        case DRM_IOCTL_SET_VERSION:    return 0;
        case DRM_IOCTL_SET_MASTER:     g_master = 1; return 0;
        case DRM_IOCTL_DROP_MASTER:    g_master = 0; return 0;
        case DRM_IOCTL_MODE_GETRESOURCES: return ioctl_getresources(arg);
        case DRM_IOCTL_MODE_GETCRTC:      return ioctl_getcrtc(arg);
        case DRM_IOCTL_MODE_SETCRTC:      return ioctl_setcrtc(arg);
        case DRM_IOCTL_MODE_GETENCODER:   return ioctl_getencoder(arg);
        case DRM_IOCTL_MODE_GETCONNECTOR: return ioctl_getconnector(arg);
        case DRM_IOCTL_MODE_CREATE_DUMB:  return ioctl_create_dumb(arg);
        case DRM_IOCTL_MODE_MAP_DUMB:     return ioctl_map_dumb(arg);
        case DRM_IOCTL_MODE_DESTROY_DUMB: return ioctl_destroy_dumb(arg);
        case DRM_IOCTL_MODE_ADDFB:        return ioctl_addfb(arg);
        case DRM_IOCTL_MODE_ADDFB2:       return ioctl_addfb2(arg);
        case DRM_IOCTL_MODE_RMFB:         return ioctl_rmfb(arg);
        case DRM_IOCTL_MODE_PAGE_FLIP:    return ioctl_page_flip(arg);
        case DRM_IOCTL_MODE_GETPLANERESOURCES: return ioctl_getplaneres(arg);
        case DRM_IOCTL_MODE_GETPLANE:     return ioctl_getplane(arg);
        case DRM_IOCTL_MODE_OBJ_GETPROPERTIES: return ioctl_obj_getprops(arg);
        case DRM_IOCTL_MODE_GETPROPERTY:   return ioctl_getprop(arg);
        case DRM_IOCTL_MODE_GETPROPBLOB:   return ioctl_getblob(arg);
        case DRM_IOCTL_MODE_CREATEPROPBLOB:  return ioctl_create_blob(arg);
        case DRM_IOCTL_MODE_DESTROYPROPBLOB: return ioctl_destroy_blob(arg);
        case DRM_IOCTL_MODE_ATOMIC:        return ioctl_atomic(arg);
        default:                          return -ENOTTY;
    }
}

static int drm_stat(vnode_t *n, vfs_stat_t *out)
{
    memset(out, 0, sizeof *out);
    out->st_ino  = n->ino;
    out->st_mode = 0666 | 0020000;
    out->st_nlink = 1;
    return 0;
}

static void drm_ref(vnode_t *n)   { (void)n; }
static void drm_unref(vnode_t *n) { (void)n; }

static const vnode_ops_t drm_ops = {
    .read      = drm_read,
    .stat      = drm_stat,
    .ref       = drm_ref,
    .unref     = drm_unref,
    .ioctl     = drm_ioctl,
    .poll      = drm_poll,
    .mmap_page = drm_mmap_page,
};

void drm_init(void)
{
    if (!global_framebuffer) return;

    memset(&g_card_node, 0, sizeof g_card_node);
    g_card_node.type     = VFS_NODE_CHARDEV;
    g_card_node.mode     = 0666;
    g_card_node.ino      = 700;
    g_card_node.rdev     = vfs_makedev(226, 0);
    g_card_node.ops      = &drm_ops;
    g_card_node.refcount = 1;

    devfs_register_in("dri", "card0", &g_card_node);
    LOG_I("[drm] /dev/dri/card0: %ux%u, one crtc, one virtual connector\n",
          fb_width(), fb_height());
}
