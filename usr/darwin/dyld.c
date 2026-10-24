#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include "darwin.h"

#define AT_MACHO_HEADER 0x10001
#define AT_MACHO_ENTRY  0x10002

#define LC_REQ_DYLD            0x80000000u
#define LC_SEGMENT_64          0x19u
#define LC_DYLD_INFO           0x22u
#define LC_DYLD_INFO_ONLY      (0x22u | LC_REQ_DYLD)
#define LC_DYLD_EXPORTS_TRIE   (0x33u | LC_REQ_DYLD)
#define LC_DYLD_CHAINED_FIXUPS (0x34u | LC_REQ_DYLD)

#define SG_READ_ONLY             0x10
#define S_MOD_INIT_FUNC_POINTERS 0x9
#define S_INIT_FUNC_OFFSETS      0x16

#define PTR_64        2
#define PTR_64_OFFSET 6

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

struct image {
    uint8_t *mh;
    intptr_t slide;
    const struct seg64 *seg[32];
    int nseg;
    const struct ledata *fixups;
    const struct dyldinfo *info;
    const uint8_t *trie;
    uint32_t trie_size;
};

int d_argc;
char **d_argv;
const char *d_progname = "";
char d_exec_path[PATH_MAX];
const void *d_mh;

static int g_debug;
static struct dprogvars g_vars;

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

static void *trie_find(const struct image *im, const char *name)
{
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
            if (flags & 0x18)
                return NULL;
            uint64_t off = uleb(&q, end);
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

static void *bind_symbol(const struct image *im, const char *name, int self, int weak)
{
    void *a = self ? trie_find(im, name) : NULL;
    if (!a)
        a = dsym_lookup(name);
    if (!a && strchr(name, '$') && !strstr(name, "$INODE64")) {
        char base[256];
        size_t n = strcspn(name, "$");
        if (n < sizeof base) {
            memcpy(base, name, n);
            base[n] = 0;
            a = dsym_lookup(base);
        }
    }
    if (a)
        return a;
    if (weak)
        return NULL;
    if (g_debug)
        fprintf(stderr, "dyld: missing %s\n", name);
    return make_trap(name);
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

static int parse(struct image *im)
{
    const struct mh64 *h = (const void *)im->mh;
    const uint8_t *p = im->mh + sizeof *h;
    const struct ledata *trie = NULL;
    int text = -1;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct lcmd *lc = (const void *)p;
        if (lc->cmdsize < sizeof *lc)
            return -1;
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
    return 0;
}

static int chained(const struct image *im)
{
    const uint8_t *base = file_ptr(im, im->fixups->dataoff);
    if (!base)
        return -1;
    const struct cf_header *h = (const void *)base;
    if (h->symbols_format) {
        fprintf(stderr, "dyld: compressed import names are not supported\n");
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
        int weak, self;
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
            self = (int8_t)(v & 0xff) <= 0;
            weak = (v >> 8) & 1;
            nameoff = v >> 9;
        } else if (h->imports_format == 3) {
            uint64_t v;
            memcpy(&v, imp + i * 16, 8);
            memcpy(&add, imp + i * 16 + 8, 8);
            self = (int16_t)(v & 0xffff) <= 0;
            weak = (v >> 16) & 1;
            nameoff = (uint32_t)(v >> 32);
        } else {
            fprintf(stderr, "dyld: unknown import format %u\n", h->imports_format);
            return -1;
        }
        targets[i] = (uint8_t *)bind_symbol(im, names + nameoff, self, weak) + add;
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
            fprintf(stderr, "dyld: unsupported chained pointer format %u\n", fmt);
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
                    if (ord >= n)
                        return -1;
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

static void protect_readonly(const struct image *im)
{
    for (int i = 0; i < im->nseg; i++) {
        const struct seg64 *s = im->seg[i];
        if ((s->flags & SG_READ_ONLY) && s->vmsize)
            mprotect((void *)(uintptr_t)(s->vmaddr + im->slide), s->vmsize, PROT_READ);
    }
}

static void run_initializers(const struct image *im, int argc, char **argv, char **envp, char **apple)
{
    typedef void (*init_fn)(int, char **, char **, char **, const struct dprogvars *);
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
                    f(argc, argv, envp, apple, &g_vars);
                }
            } else if (type == S_INIT_FUNC_OFFSETS) {
                for (uint64_t k = 0; k + 4 <= sc[j].size; k += 4) {
                    uint32_t o;
                    memcpy(&o, addr + k, 4);
                    ((init_fn)(void *)(im->mh + o))(argc, argv, envp, apple, &g_vars);
                }
            }
        }
    }
}

extern char **environ;

int main(int argc, char **argv)
{
    struct image im = { 0 };
    im.mh = (uint8_t *)getauxval(AT_MACHO_HEADER);
    uintptr_t entry = getauxval(AT_MACHO_ENTRY);
    if (!im.mh || !entry) {
        fprintf(stderr, "dyld: not started by the kernel for a Mach-O image\n");
        return 127;
    }
    const char *dbg = getenv("DYLD_DEBUG");
    g_debug = dbg && *dbg && *dbg != '0';
    d_argc = argc;
    d_argv = argv;
    d_mh = im.mh;
    if (argc > 0 && argv[0]) {
        const char *sl = strrchr(argv[0], '/');
        d_progname = sl ? sl + 1 : argv[0];
    }
    ssize_t el = readlink("/proc/self/exe", d_exec_path, sizeof d_exec_path - 1);
    if (el > 0)
        d_exec_path[el] = 0;
    else if (argc > 0 && argv[0])
        snprintf(d_exec_path, sizeof d_exec_path, "%s", argv[0]);

    if (parse(&im) < 0) {
        fprintf(stderr, "dyld: malformed Mach-O image\n");
        return 127;
    }
    d_misc_init();
    d_stdio_init();
    d_ctype_init();
    int rc = 0;
    if (im.fixups)
        rc = chained(&im);
    else if (im.info)
        fprintf(stderr, "dyld: LC_DYLD_INFO images are not supported yet\n"), rc = -1;
    if (rc < 0) {
        fprintf(stderr, "dyld: failed to bind %s\n", d_exec_path);
        return 127;
    }
    protect_readonly(&im);

    static char apple0[PATH_MAX + 16];
    snprintf(apple0, sizeof apple0, "executable_path=%s", d_exec_path);
    static char *apple[] = { apple0, NULL };
    g_vars.mh = im.mh;
    g_vars.argcp = &d_argc;
    g_vars.argvp = &d_argv;
    g_vars.envp = &environ;
    g_vars.prognamep = &d_progname;

    run_initializers(&im, argc, argv, environ, apple);
    int (*m)(int, char **, char **, char **) = (int (*)(int, char **, char **, char **))entry;
    exit(m(argc, argv, environ, apple));
}
