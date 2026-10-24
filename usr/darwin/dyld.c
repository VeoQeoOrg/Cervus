#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include "darwin.h"

#define AT_MACHO_HEADER 0x10001
#define AT_MACHO_ENTRY  0x10002

#define PAGE 4096ull

#define MH_MAGIC_64   0xfeedfacfu
#define FAT_CIGAM     0xbebafecau
#define FAT_CIGAM_64  0xbfbafecau
#define CPU_X86_64    0x01000007u
#define MH_DYLIB      6
#define MH_BUNDLE     8

#define LC_REQ_DYLD            0x80000000u
#define LC_SEGMENT_64          0x19u
#define LC_SYMTAB              0x02u
#define LC_LOAD_DYLIB          0x0cu
#define LC_ID_DYLIB            0x0du
#define LC_LOAD_WEAK_DYLIB     (0x18u | LC_REQ_DYLD)
#define LC_RPATH               (0x1cu | LC_REQ_DYLD)
#define LC_REEXPORT_DYLIB      (0x1fu | LC_REQ_DYLD)
#define LC_LAZY_LOAD_DYLIB     0x20u
#define LC_DYLD_INFO           0x22u
#define LC_DYLD_INFO_ONLY      (0x22u | LC_REQ_DYLD)
#define LC_LOAD_UPWARD_DYLIB   (0x23u | LC_REQ_DYLD)
#define LC_DYLD_EXPORTS_TRIE   (0x33u | LC_REQ_DYLD)
#define LC_DYLD_CHAINED_FIXUPS (0x34u | LC_REQ_DYLD)

#define SG_READ_ONLY             0x10
#define S_MOD_INIT_FUNC_POINTERS 0x9
#define S_THREAD_LOCAL_REGULAR   0x11
#define S_THREAD_LOCAL_ZEROFILL  0x12
#define S_THREAD_LOCAL_VARIABLES 0x13
#define S_INIT_FUNC_OFFSETS      0x16

#define PTR_64        2
#define PTR_64_OFFSET 6

#define DEP_NORMAL   0
#define DEP_WEAK     1
#define DEP_REEXPORT 2
#define DEP_UPWARD   3

#define MAX_DEPS  64
#define MAX_TLV   64

struct mh64 {
    uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};

struct lcmd {
    uint32_t cmd, cmdsize;
};

struct seg64 {
    uint32_t cmd, cmdsize;
    char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    uint32_t maxprot, initprot, nsects, flags;
};

struct sect64 {
    char sectname[16], segname[16];
    uint64_t addr, size;
    uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2, reserved3;
};

struct ledata {
    uint32_t cmd, cmdsize, dataoff, datasize;
};

struct dyldinfo {
    uint32_t cmd, cmdsize;
    uint32_t rebase_off, rebase_size, bind_off, bind_size;
    uint32_t weak_bind_off, weak_bind_size, lazy_bind_off, lazy_bind_size;
    uint32_t export_off, export_size;
};

struct symtab {
    uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};

struct nlist64 {
    uint32_t n_strx;
    uint8_t n_type, n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

struct cf_header {
    uint32_t version, starts_offset, imports_offset, symbols_offset;
    uint32_t imports_count, imports_format, symbols_format;
} __attribute__((packed));

struct cf_seg {
    uint32_t size;
    uint16_t page_size, pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count;
    uint16_t page_start[];
} __attribute__((packed));

struct tlvdesc {
    void *thunk;
    unsigned long key;
    unsigned long offset;
};

struct image {
    struct image *next;
    struct image *loader;
    char *path;
    uint8_t *mh;
    intptr_t slide;
    const struct seg64 *seg[32];
    int nseg;
    const struct ledata *fixups;
    const struct dyldinfo *info;
    const struct symtab *symtab;
    const uint8_t *trie;
    uint32_t trie_size;
    int virt;
    int ndeps;
    struct image *deps[MAX_DEPS];
    uint8_t dep_kind[MAX_DEPS];
    const char *rpath[16];
    int nrpath;
    int state;
};

struct tlvimage {
    const uint8_t *tmpl;
    size_t size;
    size_t init;
};

extern char **environ;

int d_argc;
char **d_argv;
const char *d_progname = "";
char d_exec_path[PATH_MAX];
const void *d_mh;

static int g_debug;
static struct dprogvars g_vars;
static char **g_apple;
static struct image *g_images, *g_main;
static struct image g_libsystem = { .virt = 1, .path = "/usr/lib/libSystem.B.dylib" };
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_dlerror[PATH_MAX + 128];
static int g_dlerror_set;
static struct tlvimage g_tlv[MAX_TLV];
static int g_ntlv;
static void (*g_add_hooks[16])(const void *, intptr_t);
static int g_nadd_hooks;

extern const struct dsym __start_dsym[], __stop_dsym[];
static const struct dsym **g_syms;
static size_t g_nsyms;

static int sym_cmp(const void *a, const void *b)
{
    return strcmp((*(const struct dsym *const *)a)->name, (*(const struct dsym *const *)b)->name);
}

void *dsym_lookup(const char *name)
{
    if (!g_syms) {
        g_nsyms = (size_t)(__stop_dsym - __start_dsym);
        g_syms = malloc((g_nsyms ? g_nsyms : 1) * sizeof *g_syms);
        if (!g_syms)
            return NULL;
        for (size_t i = 0; i < g_nsyms; i++)
            g_syms[i] = &__start_dsym[i];
        qsort(g_syms, g_nsyms, sizeof *g_syms, sym_cmp);
    }
    size_t lo = 0, hi = g_nsyms;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(name, g_syms[mid]->name);
        if (!c)
            return g_syms[mid]->addr;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

static void *system_lookup(const char *name)
{
    void *a = dsym_lookup(name);
    if (!a && strchr(name, '$') && !strstr(name, "$INODE64")) {
        char base[256];
        size_t n = strcspn(name, "$");
        if (n < sizeof base) {
            memcpy(base, name, n);
            base[n] = 0;
            a = dsym_lookup(base);
        }
    }
    return a;
}

static void missing_called(const char *name)
{
    fflush(stdout);
    fprintf(stderr, "dyld[%d]: Symbol not found: %s\n", getpid(), name);
    _exit(134);
}

static void *make_trap(const char *name)
{
    static uint8_t *page;
    static size_t used;
    if (!page || used + 32 > 65536) {
        page = mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) {
            page = NULL;
            return NULL;
        }
        used = 0;
    }
    uint8_t *t = page + used;
    uint64_t v;
    used += 32;
    t[0] = 0x48;
    t[1] = 0xbf;
    v = (uint64_t)(uintptr_t)name;
    memcpy(t + 2, &v, 8);
    t[10] = 0x48;
    t[11] = 0xb8;
    v = (uint64_t)(uintptr_t)missing_called;
    memcpy(t + 12, &v, 8);
    t[20] = 0xff;
    t[21] = 0xe0;
    return t;
}

static uint64_t uleb(const uint8_t **pp, const uint8_t *end)
{
    uint64_t v = 0;
    int sh = 0;
    while (*pp < end) {
        uint8_t b = *(*pp)++;
        if (sh < 64)
            v |= (uint64_t)(b & 0x7f) << sh;
        sh += 7;
        if (!(b & 0x80))
            break;
    }
    return v;
}

static int64_t sleb(const uint8_t **pp, const uint8_t *end)
{
    int64_t v = 0;
    int sh = 0;
    uint8_t b = 0;
    while (*pp < end) {
        b = *(*pp)++;
        if (sh < 64)
            v |= (int64_t)(b & 0x7f) << sh;
        sh += 7;
        if (!(b & 0x80))
            break;
    }
    if (sh < 64 && (b & 0x40))
        v |= -((int64_t)1 << sh);
    return v;
}

static struct image *find_image_by_addr(const void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    for (struct image *im = g_images; im; im = im->next)
        for (int i = 0; i < im->nseg; i++) {
            const struct seg64 *s = im->seg[i];
            uintptr_t lo = (uintptr_t)(s->vmaddr + im->slide);
            if (s->vmsize && s->initprot && a >= lo && a < lo + s->vmsize)
                return im;
        }
    return NULL;
}

static uint8_t *file_ptr(const struct image *im, uint64_t off)
{
    for (int i = 0; i < im->nseg; i++) {
        const struct seg64 *s = im->seg[i];
        if (s->filesize && off >= s->fileoff && off < s->fileoff + s->filesize)
            return (uint8_t *)(uintptr_t)(s->vmaddr + im->slide + (off - s->fileoff));
    }
    return NULL;
}

static void *resolve_in(struct image *im, const char *name, int depth);

static void *trie_resolve(struct image *im, const char *name, int depth, int *found)
{
    *found = 0;
    if (!im->trie)
        return NULL;
    const uint8_t *start = im->trie, *end = start + im->trie_size, *p = start;
    const char *s = name;
    for (;;) {
        if (p >= end)
            return NULL;
        const uint8_t *q = p;
        uint64_t tsize = uleb(&q, end);
        if (!*s && tsize) {
            uint64_t flags = uleb(&q, end);
            *found = 1;
            if (flags & 0x08) {
                uint64_t ord = uleb(&q, end);
                const char *imp = (const char *)q;
                struct image *d = ord >= 1 && ord <= (uint64_t)im->ndeps ? im->deps[ord - 1] : NULL;
                return resolve_in(d, *imp ? imp : name, depth + 1);
            }
            uint64_t off = uleb(&q, end);
            if (flags & 0x10) {
                uint64_t res = uleb(&q, end);
                return ((void *(*)(void))(void *)(im->mh + res))();
            }
            if ((flags & 3) == 2)
                return (void *)(uintptr_t)off;
            return im->mh + off;
        }
        q += tsize;
        if (q >= end)
            return NULL;
        uint8_t nchild = *q++;
        const uint8_t *next = NULL;
        for (uint8_t i = 0; i < nchild && q < end; i++) {
            const char *edge = (const char *)q;
            size_t el = strnlen(edge, (size_t)(end - q));
            q += el + 1;
            uint64_t child = uleb(&q, end);
            if (strncmp(s, edge, el) == 0) {
                next = start + child;
                s += el;
                break;
            }
        }
        if (!next)
            return NULL;
        p = next;
    }
}

static void *resolve_in(struct image *im, const char *name, int depth)
{
    if (!im || depth > 16)
        return NULL;
    if (im->virt)
        return system_lookup(name);
    int found;
    void *a = trie_resolve(im, name, depth, &found);
    if (found)
        return a;
    for (int i = 0; i < im->ndeps; i++)
        if (im->dep_kind[i] == DEP_REEXPORT && (a = resolve_in(im->deps[i], name, depth + 1)))
            return a;
    return NULL;
}

static void *flat_lookup(const char *name)
{
    for (struct image *im = g_images; im; im = im->next) {
        int found;
        void *a = trie_resolve(im, name, 0, &found);
        if (found && a)
            return a;
    }
    return system_lookup(name);
}

static void *bind_import(struct image *im, int ord, const char *name, int weak)
{
    void *a = NULL;
    if (ord > 0) {
        if (ord <= im->ndeps && im->deps[ord - 1])
            a = resolve_in(im->deps[ord - 1], name, 0);
        else if (weak)
            return NULL;
    } else if (ord == 0) {
        a = resolve_in(im, name, 0);
    } else if (ord == -1) {
        a = resolve_in(g_main, name, 0);
    }
    if (!a)
        a = flat_lookup(name);
    if (a)
        return a;
    if (weak)
        return NULL;
    if (g_debug)
        fprintf(stderr, "dyld: %s: missing %s\n", im->path, name);
    return make_trap(name);
}

static int chained(struct image *im)
{
    const uint8_t *base = file_ptr(im, im->fixups->dataoff);
    if (!base)
        return -1;
    const struct cf_header *h = (const void *)base;
    if (h->symbols_format) {
        fprintf(stderr, "dyld: %s: compressed import names are not supported\n", im->path);
        return -1;
    }
    uint32_t n = h->imports_count;
    void **targets = calloc(n ? n : 1, sizeof *targets);
    if (!targets)
        return -1;
    const char *names = (const char *)base + h->symbols_offset;
    const uint8_t *imp = base + h->imports_offset;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t nameoff;
        int weak, ord;
        int64_t add = 0;
        if (h->imports_format == 1 || h->imports_format == 2) {
            uint32_t v;
            size_t stride = h->imports_format == 1 ? 4 : 8;
            memcpy(&v, imp + i * stride, 4);
            if (h->imports_format == 2) {
                int32_t a;
                memcpy(&a, imp + i * stride + 4, 4);
                add = a;
            }
            ord = v & 0xff;
            if (ord > 0xf0)
                ord = (int8_t)ord;
            weak = (v >> 8) & 1;
            nameoff = v >> 9;
        } else if (h->imports_format == 3) {
            uint64_t v;
            memcpy(&v, imp + i * 16, 8);
            memcpy(&add, imp + i * 16 + 8, 8);
            ord = v & 0xffff;
            if (ord > 0xfff0)
                ord = (int16_t)ord;
            weak = (v >> 16) & 1;
            nameoff = (uint32_t)(v >> 32);
        } else {
            fprintf(stderr, "dyld: %s: unknown import format %u\n", im->path, h->imports_format);
            free(targets);
            return -1;
        }
        void *t = bind_import(im, ord, names + nameoff, weak);
        targets[i] = t ? (uint8_t *)t + add : NULL;
    }

    const uint8_t *starts = base + h->starts_offset;
    uint32_t nsegs;
    memcpy(&nsegs, starts, 4);
    for (uint32_t s = 0; s < nsegs; s++) {
        uint32_t off;
        memcpy(&off, starts + 4 + s * 4, 4);
        if (!off)
            continue;
        const struct cf_seg *cs = (const void *)(starts + off);
        uint16_t fmt = cs->pointer_format;
        if (fmt != PTR_64 && fmt != PTR_64_OFFSET) {
            fprintf(stderr, "dyld: %s: unsupported chained pointer format %u\n", im->path, fmt);
            free(targets);
            return -1;
        }
        for (uint16_t pg = 0; pg < cs->page_count; pg++) {
            uint16_t ps = cs->page_start[pg];
            if (ps == 0xffff)
                continue;
            uint8_t *loc = im->mh + cs->segment_offset + (uint64_t)pg * cs->page_size + ps;
            for (;;) {
                uint64_t v, out;
                memcpy(&v, loc, 8);
                uint64_t next = (v >> 51) & 0xfff;
                if (v >> 63) {
                    uint32_t ord = (uint32_t)(v & 0xffffff);
                    if (ord >= n) {
                        free(targets);
                        return -1;
                    }
                    out = (uint64_t)(uintptr_t)targets[ord] + ((v >> 24) & 0xff);
                } else {
                    uint64_t target = v & 0xfffffffffull;
                    uint64_t high8 = (v >> 36) & 0xff;
                    if (fmt == PTR_64_OFFSET)
                        out = (uint64_t)(uintptr_t)im->mh + target;
                    else
                        out = target + (uint64_t)im->slide;
                    out |= high8 << 56;
                }
                memcpy(loc, &out, 8);
                if (!next)
                    break;
                loc += next * 4;
            }
        }
    }
    free(targets);
    return 0;
}

static uint64_t *seg_loc(const struct image *im, int seg, uint64_t off)
{
    if (seg < 0 || seg >= im->nseg)
        return NULL;
    return (uint64_t *)(uintptr_t)(im->seg[seg]->vmaddr + im->slide + off);
}

static int do_rebase(struct image *im, const uint8_t *p, const uint8_t *end)
{
    int seg = 0;
    uint64_t off = 0, c, skip;
    uint64_t *loc;
    while (p < end) {
        uint8_t b = *p++, imm = b & 0x0f;
        switch (b & 0xf0) {
        case 0x00:
            return 0;
        case 0x10:
            break;
        case 0x20:
            seg = imm;
            off = uleb(&p, end);
            break;
        case 0x30:
            off += uleb(&p, end);
            break;
        case 0x40:
            off += (uint64_t)imm * 8;
            break;
        case 0x50:
        case 0x60:
            c = (b & 0xf0) == 0x50 ? imm : uleb(&p, end);
            while (c--) {
                if ((loc = seg_loc(im, seg, off)))
                    *loc += (uint64_t)im->slide;
                off += 8;
            }
            break;
        case 0x70:
            if ((loc = seg_loc(im, seg, off)))
                *loc += (uint64_t)im->slide;
            off += uleb(&p, end) + 8;
            break;
        case 0x80:
            c = uleb(&p, end);
            skip = uleb(&p, end);
            while (c--) {
                if ((loc = seg_loc(im, seg, off)))
                    *loc += (uint64_t)im->slide;
                off += skip + 8;
            }
            break;
        default:
            return -1;
        }
    }
    return 0;
}

static int do_bind(struct image *im, const uint8_t *p, const uint8_t *end, int lazy, int weakmode)
{
    int seg = 0, ord = 0, flags = 0;
    uint64_t off = 0, c;
    int64_t addend = 0;
    const char *name = NULL, *cached_name = NULL;
    int cached_ord = 0;
    void *target = NULL;
    while (p < end) {
        uint8_t b = *p++, imm = b & 0x0f;
        int op = b & 0xf0, binds = 0;
        uint64_t step = 8;
        switch (op) {
        case 0x00:
            if (lazy)
                break;
            return 0;
        case 0x10:
            ord = imm;
            break;
        case 0x20:
            ord = (int)uleb(&p, end);
            break;
        case 0x30:
            ord = imm ? (int)(int8_t)(0xf0 | imm) : 0;
            break;
        case 0x40:
            flags = imm;
            name = (const char *)p;
            p += strnlen(name, (size_t)(end - p)) + 1;
            break;
        case 0x50:
            break;
        case 0x60:
            addend = sleb(&p, end);
            break;
        case 0x70:
            seg = imm;
            off = uleb(&p, end);
            break;
        case 0x80:
            off += uleb(&p, end);
            break;
        case 0x90:
            binds = 1;
            break;
        case 0xa0:
            binds = 1;
            step = uleb(&p, end) + 8;
            break;
        case 0xb0:
            binds = 1;
            step = (uint64_t)imm * 8 + 8;
            break;
        case 0xc0:
            binds = (int)uleb(&p, end);
            step = uleb(&p, end) + 8;
            break;
        default:
            return -1;
        }
        if (!binds || !name)
            continue;
        if (name != cached_name || ord != cached_ord) {
            cached_name = name;
            cached_ord = ord;
            if (weakmode) {
                target = (flags & 0x8) ? NULL : flat_lookup(name);
            } else {
                target = bind_import(im, ord, name, flags & 1);
            }
        }
        for (c = (uint64_t)binds; c; c--) {
            uint64_t *loc = seg_loc(im, seg, off);
            if (loc && (target || !weakmode))
                *loc = (uint64_t)(uintptr_t)target + (target ? (uint64_t)addend : 0);
            off += step;
        }
    }
    return 0;
}

static int dyld_info(struct image *im)
{
    const struct dyldinfo *di = im->info;
    const uint8_t *p;
    if (di->rebase_size && (p = file_ptr(im, di->rebase_off)) && do_rebase(im, p, p + di->rebase_size) < 0)
        return -1;
    if (di->bind_size && (p = file_ptr(im, di->bind_off)) && do_bind(im, p, p + di->bind_size, 0, 0) < 0)
        return -1;
    if (di->lazy_bind_size && (p = file_ptr(im, di->lazy_bind_off)) && do_bind(im, p, p + di->lazy_bind_size, 1, 0) < 0)
        return -1;
    if (di->weak_bind_size && (p = file_ptr(im, di->weak_bind_off)) && do_bind(im, p, p + di->weak_bind_size, 0, 1) < 0)
        return -1;
    return 0;
}

static __thread uint8_t *t_tlv[MAX_TLV];

void *d_tlv_lookup(struct tlvdesc *d)
{
    unsigned long k = d->key;
    if (k >= MAX_TLV)
        return NULL;
    uint8_t *b = t_tlv[k];
    if (!b) {
        const struct tlvimage *ti = &g_tlv[k];
        b = malloc(ti->size ? ti->size : 1);
        if (!b)
            abort();
        memcpy(b, ti->tmpl, ti->init);
        memset(b + ti->init, 0, ti->size - ti->init);
        t_tlv[k] = b;
    }
    return b + d->offset;
}

__asm__(
    ".text\n"
    ".globl d_tlv_get_addr\n"
    "d_tlv_get_addr:\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    call d_tlv_lookup\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    ret\n");
void d_tlv_get_addr(void);
DSYM("__tlv_bootstrap", d_tlv_get_addr);

static void d_tlv_atexit(void (*fn)(void *), void *obj)
{
    (void)fn;
    (void)obj;
}
DSYM("__tlv_atexit", d_tlv_atexit);

static void d_stub_binder(void)
{
    fprintf(stderr, "dyld: unexpected lazy binding request\n");
    abort();
}
DSYM("dyld_stub_binder", d_stub_binder);

static void setup_tlv(struct image *im)
{
    uint8_t *lo = NULL, *hi = NULL, *init = NULL;
    for (int i = 0; i < im->nseg; i++) {
        const struct sect64 *sc = (const void *)(im->seg[i] + 1);
        for (uint32_t j = 0; j < im->seg[i]->nsects; j++) {
            uint32_t t = sc[j].flags & 0xff;
            if (t != S_THREAD_LOCAL_REGULAR && t != S_THREAD_LOCAL_ZEROFILL)
                continue;
            uint8_t *a = (uint8_t *)(uintptr_t)(sc[j].addr + im->slide);
            if (!lo || a < lo)
                lo = a;
            if (a + sc[j].size > hi)
                hi = a + sc[j].size;
            if (t == S_THREAD_LOCAL_REGULAR && a + sc[j].size > init)
                init = a + sc[j].size;
        }
    }
    int idx = -1;
    for (int i = 0; i < im->nseg; i++) {
        const struct sect64 *sc = (const void *)(im->seg[i] + 1);
        for (uint32_t j = 0; j < im->seg[i]->nsects; j++) {
            if ((sc[j].flags & 0xff) != S_THREAD_LOCAL_VARIABLES)
                continue;
            if (idx < 0) {
                if (g_ntlv >= MAX_TLV) {
                    fprintf(stderr, "dyld: too many images with thread-local variables\n");
                    abort();
                }
                idx = g_ntlv++;
                g_tlv[idx].tmpl = lo;
                g_tlv[idx].size = lo ? (size_t)(hi - lo) : 0;
                g_tlv[idx].init = lo && init ? (size_t)(init - lo) : 0;
            }
            struct tlvdesc *d = (struct tlvdesc *)(uintptr_t)(sc[j].addr + im->slide);
            for (uint64_t k = 0; k < sc[j].size / sizeof *d; k++) {
                d[k].thunk = (void *)d_tlv_get_addr;
                d[k].key = (unsigned long)idx;
            }
        }
    }
}

static void protect_readonly(const struct image *im)
{
    for (int i = 0; i < im->nseg; i++) {
        const struct seg64 *s = im->seg[i];
        if ((s->flags & SG_READ_ONLY) && s->vmsize)
            mprotect((void *)(uintptr_t)(s->vmaddr + im->slide), s->vmsize, PROT_READ);
    }
}

static int is_system_dylib(const char *n)
{
    static const char *const names[] = {
        "/usr/lib/libSystem.B.dylib", "/usr/lib/libSystem.dylib", "/usr/lib/libc.dylib",
        "/usr/lib/libm.dylib", "/usr/lib/libdl.dylib", "/usr/lib/libpthread.dylib",
        "/usr/lib/libgcc_s.1.dylib", "/usr/lib/libutil.dylib", "/usr/lib/libiconv.2.dylib",
        "/usr/lib/libcharset.1.dylib", "/usr/lib/libresolv.9.dylib", "/usr/lib/libinfo.dylib",
        "/usr/lib/libpoll.dylib", "/usr/lib/libproc.dylib", "/usr/lib/librpcsvc.dylib",
        "/usr/lib/libedit.3.dylib", "/usr/lib/libedit.2.dylib", "/usr/lib/libedit.dylib",
        "/usr/lib/libreadline.dylib",
    };
    if (!strncmp(n, "/usr/lib/system/", 16))
        return 1;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(n, names[i]))
            return 1;
    return 0;
}

static int parse(struct image *im, const char ***dylibs, uint8_t **kinds, int *ndylibs)
{
    const struct mh64 *h = (const void *)im->mh;
    const uint8_t *p = im->mh + sizeof *h;
    const struct ledata *trie = NULL;
    int text = -1, n = 0;
    static const char *names[MAX_DEPS];
    static uint8_t kind[MAX_DEPS];
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct lcmd *lc = (const void *)p;
        if (lc->cmdsize < sizeof *lc)
            return -1;
        uint32_t off;
        switch (lc->cmd) {
        case LC_SEGMENT_64:
            if (im->nseg < 32) {
                const struct seg64 *s = (const void *)p;
                if (s->fileoff == 0 && s->filesize)
                    text = im->nseg;
                im->seg[im->nseg++] = s;
            }
            break;
        case LC_DYLD_CHAINED_FIXUPS:
            im->fixups = (const void *)p;
            break;
        case LC_DYLD_INFO:
        case LC_DYLD_INFO_ONLY:
            im->info = (const void *)p;
            break;
        case LC_DYLD_EXPORTS_TRIE:
            trie = (const void *)p;
            break;
        case LC_SYMTAB:
            im->symtab = (const void *)p;
            break;
        case LC_RPATH:
            memcpy(&off, p + 8, 4);
            if (im->nrpath < 16 && off < lc->cmdsize)
                im->rpath[im->nrpath++] = (const char *)p + off;
            break;
        case LC_LOAD_DYLIB:
        case LC_LOAD_WEAK_DYLIB:
        case LC_REEXPORT_DYLIB:
        case LC_LAZY_LOAD_DYLIB:
        case LC_LOAD_UPWARD_DYLIB:
            memcpy(&off, p + 8, 4);
            if (n < MAX_DEPS && off < lc->cmdsize) {
                names[n] = (const char *)p + off;
                kind[n] = lc->cmd == LC_LOAD_WEAK_DYLIB ? DEP_WEAK
                        : lc->cmd == LC_REEXPORT_DYLIB ? DEP_REEXPORT
                        : lc->cmd == LC_LOAD_UPWARD_DYLIB ? DEP_UPWARD : DEP_NORMAL;
                n++;
            }
            break;
        }
        p += lc->cmdsize;
    }
    if (text < 0)
        return -1;
    im->slide = (intptr_t)im->mh - (intptr_t)im->seg[text]->vmaddr;
    if (trie && trie->datasize) {
        im->trie = file_ptr(im, trie->dataoff);
        im->trie_size = trie->datasize;
    } else if (im->info && im->info->export_size) {
        im->trie = file_ptr(im, im->info->export_off);
        im->trie_size = im->info->export_size;
    }
    *dylibs = names;
    *kinds = kind;
    *ndylibs = n;
    return 0;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint8_t *map_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    uint8_t hdr[4096];
    uint8_t *lc = NULL, *base = MAP_FAILED, *mh = NULL;
    uint64_t slice = 0;
    ssize_t n = pread(fd, hdr, sizeof hdr, 0);
    if (n < (ssize_t)sizeof(struct mh64))
        goto out;
    uint32_t magic;
    memcpy(&magic, hdr, 4);
    if (magic == FAT_CIGAM || magic == FAT_CIGAM_64) {
        uint32_t nfat = be32(hdr + 4);
        size_t esz = magic == FAT_CIGAM_64 ? 32 : 20;
        for (uint32_t i = 0; i < nfat && 8 + (i + 1) * esz <= (size_t)n; i++) {
            const uint8_t *e = hdr + 8 + i * esz;
            if (be32(e) != CPU_X86_64)
                continue;
            slice = magic == FAT_CIGAM_64 ? ((uint64_t)be32(e + 8) << 32 | be32(e + 12)) : be32(e + 8);
            break;
        }
        if (!slice)
            goto out;
        n = pread(fd, hdr, sizeof hdr, (off_t)slice);
        if (n < (ssize_t)sizeof(struct mh64))
            goto out;
    }
    const struct mh64 *h = (const void *)hdr;
    if (h->magic != MH_MAGIC_64 || h->cputype != CPU_X86_64 ||
        (h->filetype != MH_DYLIB && h->filetype != MH_BUNDLE))
        goto out;
    size_t lcsize = sizeof *h + h->sizeofcmds;
    if (lcsize <= (size_t)n) {
        lc = hdr;
    } else {
        lc = malloc(lcsize);
        if (!lc || pread(fd, lc, lcsize, (off_t)slice) != (ssize_t)lcsize)
            goto out;
    }
    uint64_t lo = UINT64_MAX, hi = 0;
    const uint8_t *p = lc + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct seg64 *s = (const void *)p;
        if (s->cmd == LC_SEGMENT_64 && s->vmsize && s->initprot) {
            if (s->vmaddr < lo)
                lo = s->vmaddr;
            if (s->vmaddr + s->vmsize > hi)
                hi = s->vmaddr + s->vmsize;
        }
        p += s->cmdsize;
    }
    if (lo >= hi)
        goto out;
    lo &= ~(PAGE - 1);
    hi = (hi + PAGE - 1) & ~(PAGE - 1);
    base = mmap(NULL, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        goto out;
    intptr_t slide = (intptr_t)base - (intptr_t)lo;
    p = lc + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct seg64 *s = (const void *)p;
        p += s->cmdsize;
        if (s->cmd != LC_SEGMENT_64 || !s->vmsize || !s->initprot)
            continue;
        uint8_t *addr = (uint8_t *)(uintptr_t)(s->vmaddr + slide);
        int prot = (s->initprot & 1 ? PROT_READ : 0) | (s->initprot & 2 ? PROT_WRITE : 0) |
                   (s->initprot & 4 ? PROT_EXEC : 0);
        uint64_t mapped = 0;
        if (s->filesize) {
            if (mmap(addr, s->filesize, prot, MAP_PRIVATE | MAP_FIXED, fd, (off_t)(slice + s->fileoff)) == MAP_FAILED)
                goto fail;
            mapped = (s->filesize + PAGE - 1) & ~(PAGE - 1);
            if (s->vmsize > s->filesize && (s->filesize & (PAGE - 1)) && (prot & PROT_WRITE))
                memset(addr + s->filesize, 0, PAGE - (s->filesize & (PAGE - 1)));
            if (s->fileoff == 0)
                mh = addr;
        }
        uint64_t total = (s->vmsize + PAGE - 1) & ~(PAGE - 1);
        if (total > mapped &&
            mmap(addr + mapped, total - mapped, prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
            goto fail;
    }
    goto out;
fail:
    munmap(base, hi - lo);
    mh = NULL;
out:
    if (lc && lc != hdr)
        free(lc);
    close(fd);
    return mh;
}

static int path_join(char *out, const char *a, size_t alen, const char *b)
{
    int r = snprintf(out, PATH_MAX, "%.*s%s", (int)alen, a, b);
    return r > 0 && r < PATH_MAX;
}

static int dir_len(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? (int)(s - p) : 0;
}

static struct image *find_loaded(const char *real)
{
    for (struct image *im = g_images; im; im = im->next)
        if (!strcmp(im->path, real))
            return im;
    return NULL;
}

static struct image *load_dylib(const char *name, struct image *loader, int kind, char *err);

static struct image *open_candidate(const char *cand, struct image *loader, char *err)
{
    char phys[PATH_MAX], real[PATH_MAX];
    if (snprintf(phys, sizeof phys, "/compat/darwin%s", cand) >= (int)sizeof phys)
        return NULL;
    const char *use = access(phys, F_OK) == 0 ? phys : access(cand, F_OK) == 0 ? cand : NULL;
    if (!use)
        return NULL;
    if (!realpath(use, real))
        snprintf(real, sizeof real, "%s", use);
    struct image *im = find_loaded(real);
    if (im)
        return im;
    uint8_t *mh = map_file(real);
    if (!mh) {
        snprintf(err, PATH_MAX + 64, "%s: not a loadable x86_64 Mach-O library", real);
        return NULL;
    }
    im = calloc(1, sizeof *im);
    if (!im)
        return NULL;
    im->mh = mh;
    im->path = strdup(real);
    im->loader = loader;
    const char **names;
    uint8_t *kinds;
    int n;
    if (parse(im, &names, &kinds, &n) < 0) {
        snprintf(err, PATH_MAX + 64, "%s: malformed Mach-O image", real);
        return NULL;
    }
    struct image **tail = &g_images;
    while (*tail)
        tail = &(*tail)->next;
    *tail = im;
    const char *dn[MAX_DEPS];
    uint8_t dk[MAX_DEPS];
    memcpy(dn, names, n * sizeof *dn);
    memcpy(dk, kinds, (size_t)n);
    im->ndeps = n;
    for (int i = 0; i < n; i++) {
        im->dep_kind[i] = dk[i];
        im->deps[i] = load_dylib(dn[i], im, dk[i], err);
        if (!im->deps[i] && dk[i] != DEP_WEAK)
            return NULL;
    }
    return im;
}

static struct image *try_with_rpaths(const char *rest, struct image *from, struct image *loader, char *err)
{
    char cand[PATH_MAX], rp[PATH_MAX];
    for (struct image *im = from; im; im = im->loader) {
        for (int i = 0; i < im->nrpath; i++) {
            const char *r = im->rpath[i];
            if (!strncmp(r, "@loader_path", 12)) {
                if (!path_join(rp, im->path, (size_t)dir_len(im->path), r + 12))
                    continue;
            } else if (!strncmp(r, "@executable_path", 16)) {
                if (!path_join(rp, d_exec_path, (size_t)dir_len(d_exec_path), r + 16))
                    continue;
            } else {
                snprintf(rp, sizeof rp, "%s", r);
            }
            if (!path_join(cand, rp, strlen(rp), rest))
                continue;
            struct image *got = open_candidate(cand, loader, err);
            if (got)
                return got;
        }
    }
    if (from != g_main && g_main)
        return try_with_rpaths(rest, g_main, loader, err);
    return NULL;
}

static struct image *load_dylib(const char *name, struct image *loader, int kind, char *err)
{
    char cand[PATH_MAX];
    struct image *im = NULL;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    (void)kind;
    if (is_system_dylib(name))
        return &g_libsystem;
    const char *env = getenv("DYLD_LIBRARY_PATH");
    if (env) {
        const char *s = env;
        while (*s && !im) {
            size_t l = strcspn(s, ":");
            if (l && snprintf(cand, sizeof cand, "%.*s/%s", (int)l, s, base) < (int)sizeof cand)
                im = open_candidate(cand, loader, err);
            s += l;
            if (*s == ':')
                s++;
        }
        if (im)
            return im;
    }
    if (!strncmp(name, "@@HOMEBREW_PREFIX@@", 19)) {
        if (path_join(cand, "/usr/local", 10, name + 19))
            im = open_candidate(cand, loader, err);
    } else if (!strncmp(name, "@@HOMEBREW_CELLAR@@", 19)) {
        if (path_join(cand, "/usr/local/Cellar", 17, name + 19))
            im = open_candidate(cand, loader, err);
    } else if (!strncmp(name, "@executable_path", 16)) {
        if (path_join(cand, d_exec_path, (size_t)dir_len(d_exec_path), name + 16))
            im = open_candidate(cand, loader, err);
    } else if (!strncmp(name, "@loader_path", 12)) {
        const char *lp = loader ? loader->path : d_exec_path;
        if (path_join(cand, lp, (size_t)dir_len(lp), name + 12))
            im = open_candidate(cand, loader, err);
    } else if (!strncmp(name, "@rpath", 6)) {
        im = try_with_rpaths(name + 6, loader ? loader : g_main, loader, err);
    } else if (strchr(name, '/')) {
        im = open_candidate(name, loader, err);
    }
    if (im)
        return im;
    const char *fb = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    char home[PATH_MAX];
    if (!fb) {
        const char *h = getenv("HOME");
        snprintf(home, sizeof home, "%s/lib:/usr/local/lib:/usr/lib", h ? h : "");
        fb = home;
    }
    for (const char *s = fb; *s && !im;) {
        size_t l = strcspn(s, ":");
        if (l && snprintf(cand, sizeof cand, "%.*s/%s", (int)l, s, base) < (int)sizeof cand)
            im = open_candidate(cand, loader, err);
        s += l;
        if (*s == ':')
            s++;
    }
    if (!im && (!strncmp(name, "/usr/lib/", 9) || !strncmp(name, "/System/Library/", 16))) {
        if (g_debug)
            fprintf(stderr, "dyld: %s is provided by the runtime\n", name);
        *err = 0;
        return &g_libsystem;
    }
    if (!im && !*err)
        snprintf(err, PATH_MAX + 64, "Library not loaded: %s (referenced from %s)", name,
                 loader ? loader->path : d_exec_path);
    return im;
}

static int bind_new(void)
{
    for (struct image *im = g_images; im; im = im->next) {
        if (im->state)
            continue;
        int rc = 0;
        if (im->fixups)
            rc = chained(im);
        else if (im->info)
            rc = dyld_info(im);
        if (rc < 0) {
            fprintf(stderr, "dyld: failed to bind %s\n", im->path);
            return -1;
        }
        setup_tlv(im);
        protect_readonly(im);
        im->state = 1;
        for (int i = 0; i < g_nadd_hooks; i++)
            g_add_hooks[i](im->mh, im->slide);
    }
    return 0;
}

static void run_initializers(struct image *im)
{
    typedef void (*init_fn)(int, char **, char **, char **, const struct dprogvars *);
    if (!im || im->virt || im->state != 1)
        return;
    im->state = 2;
    for (int i = 0; i < im->ndeps; i++)
        if (im->dep_kind[i] != DEP_UPWARD)
            run_initializers(im->deps[i]);
    for (int i = 0; i < im->nseg; i++) {
        const struct seg64 *s = im->seg[i];
        const struct sect64 *sc = (const void *)(s + 1);
        for (uint32_t j = 0; j < s->nsects; j++) {
            uint32_t type = sc[j].flags & 0xff;
            uint8_t *addr = (uint8_t *)(uintptr_t)(sc[j].addr + im->slide);
            if (type == S_MOD_INIT_FUNC_POINTERS) {
                for (uint64_t k = 0; k + 8 <= sc[j].size; k += 8) {
                    init_fn f;
                    memcpy(&f, addr + k, 8);
                    f(d_argc, d_argv, environ, g_apple, &g_vars);
                }
            } else if (type == S_INIT_FUNC_OFFSETS) {
                for (uint64_t k = 0; k + 4 <= sc[j].size; k += 4) {
                    uint32_t o;
                    memcpy(&o, addr + k, 4);
                    ((init_fn)(void *)(im->mh + o))(d_argc, d_argv, environ, g_apple, &g_vars);
                }
            }
        }
    }
}

#define DRTLD_NOLOAD   0x10
#define DRTLD_NEXT     ((void *)-1)
#define DRTLD_DEFAULT  ((void *)-2)
#define DRTLD_SELF     ((void *)-3)
#define DRTLD_MAIN     ((void *)-5)

static void set_dlerror(const char *msg)
{
    snprintf(g_dlerror, sizeof g_dlerror, "%s", msg);
    g_dlerror_set = 1;
}

static void *d_dlopen(const char *path, int mode)
{
    if (!path)
        return g_main;
    char err[PATH_MAX + 128] = "";
    pthread_mutex_lock(&g_lock);
    struct image *im;
    if (mode & DRTLD_NOLOAD) {
        char real[PATH_MAX];
        im = is_system_dylib(path) ? &g_libsystem : realpath(path, real) ? find_loaded(real) : NULL;
    } else {
        im = load_dylib(path, NULL, DEP_NORMAL, err);
        if (im && bind_new() < 0)
            im = NULL;
    }
    pthread_mutex_unlock(&g_lock);
    if (!im) {
        if (!(mode & DRTLD_NOLOAD))
            set_dlerror(*err ? err : "image not found");
        return NULL;
    }
    run_initializers(im);
    return im;
}

static int d_dlclose(void *h)
{
    (void)h;
    return 0;
}

static void *search_deps(struct image *im, const char *name, int depth)
{
    if (!im || depth > 8)
        return NULL;
    void *a = resolve_in(im, name, 0);
    for (int i = 0; !a && i < im->ndeps; i++)
        a = search_deps(im->deps[i], name, depth + 1);
    return a;
}

static void *d_dlsym(void *h, const char *sym)
{
    char name[512];
    if (snprintf(name, sizeof name, "_%s", sym) >= (int)sizeof name)
        return NULL;
    void *a;
    if (h == DRTLD_DEFAULT || h == DRTLD_NEXT || h == g_main)
        a = flat_lookup(name);
    else if (h == DRTLD_SELF)
        a = flat_lookup(name);
    else if (h == DRTLD_MAIN)
        a = resolve_in(g_main, name, 0);
    else
        a = search_deps(h, name, 0);
    if (!a) {
        char msg[600];
        snprintf(msg, sizeof msg, "dlsym(%p, %s): symbol not found", h, sym);
        set_dlerror(msg);
    }
    return a;
}

static char *d_dlerror(void)
{
    if (!g_dlerror_set)
        return NULL;
    g_dlerror_set = 0;
    return g_dlerror;
}

struct ddlinfo {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
};

static int d_dladdr(const void *addr, struct ddlinfo *info)
{
    memset(info, 0, sizeof *info);
    struct image *im = find_image_by_addr(addr);
    if (!im)
        return 0;
    info->dli_fname = im->path;
    info->dli_fbase = im->mh;
    if (im->symtab) {
        const struct nlist64 *nl = (const void *)file_ptr(im, im->symtab->symoff);
        const char *str = (const char *)file_ptr(im, im->symtab->stroff);
        uintptr_t best = 0;
        for (uint32_t i = 0; nl && str && i < im->symtab->nsyms; i++) {
            if ((nl[i].n_type & 0xe0) || (nl[i].n_type & 0x0e) != 0x0e)
                continue;
            uintptr_t v = (uintptr_t)(nl[i].n_value + im->slide);
            if (v <= (uintptr_t)addr && v >= best) {
                best = v;
                const char *s = str + nl[i].n_strx;
                info->dli_sname = *s == '_' ? s + 1 : s;
                info->dli_saddr = (void *)v;
            }
        }
    }
    return 1;
}

DSYM("_dlopen", d_dlopen);
DSYM("_dlclose", d_dlclose);
DSYM("_dlsym", d_dlsym);
DSYM("_dlerror", d_dlerror);
DSYM("_dladdr", d_dladdr);

static struct image *image_at(uint32_t idx)
{
    struct image *im = g_images;
    while (im && idx--)
        im = im->next;
    return im;
}

static uint32_t d_dyld_image_count(void)
{
    uint32_t n = 0;
    for (struct image *im = g_images; im; im = im->next)
        n++;
    return n;
}

static const void *d_dyld_get_image_header(uint32_t i)
{
    struct image *im = image_at(i);
    return im ? im->mh : NULL;
}

static const char *d_dyld_get_image_name(uint32_t i)
{
    struct image *im = image_at(i);
    return im ? im->path : NULL;
}

static intptr_t d_dyld_get_image_vmaddr_slide(uint32_t i)
{
    struct image *im = image_at(i);
    return im ? im->slide : 0;
}

static intptr_t d_dyld_get_image_slide(const void *mh)
{
    for (struct image *im = g_images; im; im = im->next)
        if (im->mh == mh)
            return im->slide;
    return 0;
}

static void d_dyld_register_func_for_add_image(void (*fn)(const void *, intptr_t))
{
    if (g_nadd_hooks < 16)
        g_add_hooks[g_nadd_hooks++] = fn;
    for (struct image *im = g_images; im; im = im->next)
        if (im->state)
            fn(im->mh, im->slide);
}

static void d_dyld_register_func_for_remove_image(void (*fn)(const void *, intptr_t))
{
    (void)fn;
}

static uint32_t d_dyld_get_program_sdk_version(void) { return 0x000e0000; }
static int d_dyld_shared_cache_some_image_overridden(void) { return 0; }

DSYM("__dyld_image_count", d_dyld_image_count);
DSYM("__dyld_get_image_header", d_dyld_get_image_header);
DSYM("__dyld_get_image_name", d_dyld_get_image_name);
DSYM("__dyld_get_image_vmaddr_slide", d_dyld_get_image_vmaddr_slide);
DSYM("__dyld_get_image_slide", d_dyld_get_image_slide);
DSYM("__dyld_register_func_for_add_image", d_dyld_register_func_for_add_image);
DSYM("__dyld_register_func_for_remove_image", d_dyld_register_func_for_remove_image);
DSYM("_dyld_get_program_sdk_version", d_dyld_get_program_sdk_version);
DSYM("_dyld_get_program_min_os_version", d_dyld_get_program_sdk_version);
DSYM("_dyld_shared_cache_some_image_overridden", d_dyld_shared_cache_some_image_overridden);

int main(int argc, char **argv)
{
    uint8_t *mh = (uint8_t *)getauxval(AT_MACHO_HEADER);
    uintptr_t entry = getauxval(AT_MACHO_ENTRY);
    if (!mh || !entry) {
        fprintf(stderr, "dyld: not started by the kernel for a Mach-O image\n");
        return 127;
    }
    const char *dbg = getenv("DYLD_DEBUG");
    g_debug = dbg && *dbg && *dbg != '0';
    d_argc = argc;
    d_argv = argv;
    d_mh = mh;
    if (argc > 0 && argv[0]) {
        const char *sl = strrchr(argv[0], '/');
        d_progname = sl ? sl + 1 : argv[0];
    }
    ssize_t el = readlink("/proc/self/exe", d_exec_path, sizeof d_exec_path - 1);
    if (el > 0)
        d_exec_path[el] = 0;
    else if (argc > 0 && argv[0])
        snprintf(d_exec_path, sizeof d_exec_path, "%s", argv[0]);

    const char *tid = getenv("TERMINFO_DIRS");
    if (!tid) {
        setenv("TERMINFO_DIRS", "/compat/darwin/usr/share/terminfo:", 0);
    } else if (!strstr(tid, "/compat/darwin/usr/share/terminfo")) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof buf, "%s:/compat/darwin/usr/share/terminfo", tid);
        setenv("TERMINFO_DIRS", buf, 1);
    }
    d_misc_init();
    d_stdio_init();
    d_ctype_init();

    static struct image main_image;
    g_main = &main_image;
    g_main->mh = mh;
    g_main->path = d_exec_path;
    const char **names;
    uint8_t *kinds;
    int n;
    if (parse(g_main, &names, &kinds, &n) < 0) {
        fprintf(stderr, "dyld: malformed Mach-O image\n");
        return 127;
    }
    g_images = g_main;
    const char *dn[MAX_DEPS];
    uint8_t dk[MAX_DEPS];
    memcpy(dn, names, n * sizeof *dn);
    memcpy(dk, kinds, (size_t)n);
    g_main->ndeps = n;
    for (int i = 0; i < n; i++) {
        char err[PATH_MAX + 128] = "";
        g_main->dep_kind[i] = dk[i];
        g_main->deps[i] = load_dylib(dn[i], g_main, dk[i], err);
        if (!g_main->deps[i] && dk[i] != DEP_WEAK) {
            fprintf(stderr, "dyld[%d]: %s\n", getpid(), *err ? err : dn[i]);
            return 134;
        }
    }
    if (bind_new() < 0)
        return 127;

    static char apple0[PATH_MAX + 16];
    snprintf(apple0, sizeof apple0, "executable_path=%s", d_exec_path);
    static char *apple[] = { apple0, NULL };
    g_apple = apple;
    g_vars.mh = mh;
    g_vars.argcp = &d_argc;
    g_vars.argvp = &d_argv;
    g_vars.envp = &environ;
    g_vars.prognamep = &d_progname;

    run_initializers(g_main);
    int (*m)(int, char **, char **, char **) = (int (*)(int, char **, char **, char **))entry;
    exit(m(argc, argv, environ, apple));
}
