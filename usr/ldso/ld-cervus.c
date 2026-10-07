typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;
typedef long               int64_t;
typedef unsigned long      uintptr_t;
typedef unsigned long      size_t;

#define NULL ((void *)0)

#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_BASE    7
#define AT_ENTRY   9

#define PT_LOAD    1
#define PT_PHDR    6
#define PT_DYNAMIC 2
#define PT_TLS     7

#define DT_NULL    0
#define DT_NEEDED  1
#define DT_PLTRELSZ 2
#define DT_PLTGOT  3
#define DT_HASH    4
#define DT_STRTAB  5
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_STRSZ   10
#define DT_SYMENT  11
#define DT_INIT    12
#define DT_FINI    13
#define DT_SONAME  14
#define DT_RPATH   15
#define DT_RUNPATH 29
#define DT_JMPREL  23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8
#define R_X86_64_DTPMOD64  16
#define R_X86_64_DTPOFF64  17
#define R_X86_64_TPOFF64   18
#define R_X86_64_TLSDESC   36
#define R_X86_64_IRELATIVE 37

#define SHN_UNDEF 0
#define STT_TLS   6
#define STB_WEAK  2

#define O_RDONLY 0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_PRIVATE   2
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

#define SYS_EXIT     0
#define SYS_READ     20
#define SYS_WRITE    21
#define SYS_OPEN     22
#define SYS_CLOSE    23
#define SYS_SEEK     24
#define SYS_MMAP     40
#define SYS_MPROTECT 42

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} ehdr_t;

typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} phdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} sym_t;

typedef struct {
    uint64_t d_tag;
    uint64_t d_val;
} dyn_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} rela_t;

#define ELF64_R_SYM(i)  ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)(i))

#define MAX_OBJECTS 512
#define MAX_NEEDED  64
#define MAX_TLS_MODS 256
#define TLS_SURPLUS  4096

typedef struct {
    uintptr_t image;
    size_t    filesz, memsz, align;
    size_t    offset;
    int       is_static;
} tls_mod_t;

typedef struct {
    size_t     nmods;
    size_t     static_size;
    size_t     static_align;
    size_t     static_used;
    uint64_t   gen;
    tls_mod_t *mods;
} tls_layout_t;

static tls_mod_t    g_tls_mods[MAX_TLS_MODS + 1];
static tls_layout_t g_tls = { 0, 0, 16, 0, 1, g_tls_mods };

typedef struct {
    const phdr_t *phdr;
    uint16_t      phnum;
    uintptr_t     tls_image;
    size_t        tls_filesz, tls_memsz, tls_align;
} map_info_t;

typedef struct {
    const char *name;
    const char *path;
    uintptr_t   base;
    const phdr_t *phdr;
    uint16_t    phnum;
    size_t      tls_modid;
    dyn_t      *dyn;
    const char *strtab;
    sym_t      *symtab;
    uint32_t   *hash;
    int         relocated;
    int         inited;
    uintptr_t   map_start;
    uintptr_t   map_end;
    int         nneeded;
    short       needed[MAX_NEEDED];
} object_t;

static object_t g_objs[MAX_OBJECTS];
static int      g_nobjs;
static const char *g_library_path;
static const char *g_fail;

static long sys1(long n, long a) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}
static long sys3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return r;
}
static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return r;
}

static size_t ld_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int ld_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void ld_copy(char *d, const char *s, size_t cap) {
    size_t i = 0;
    while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void ld_say(const char *s) { sys3(SYS_WRITE, 2, (long)s, (long)ld_strlen(s)); }

static void ld_die(const char *what, const char *detail) {
    ld_say("ld-cervus: ");
    ld_say(what);
    if (detail) { ld_say(": "); ld_say(detail); }
    ld_say("\n");
    sys1(SYS_EXIT, 127);
    for (;;) { }
}

static uint32_t elf_hash(const char *name) {
    uint32_t h = 0, g;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h = (h << 4) + *p;
        g = h & 0xF0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static void scan_dynamic(object_t *o) {
    o->strtab = NULL;
    o->symtab = NULL;
    o->hash   = NULL;
    for (dyn_t *d = o->dyn; d && d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_STRTAB: o->strtab = (const char *)(o->base + d->d_val); break;
            case DT_SYMTAB: o->symtab = (sym_t *)(o->base + d->d_val); break;
            case DT_HASH:   o->hash   = (uint32_t *)(o->base + d->d_val); break;
            default: break;
        }
    }
}

static sym_t *lookup_in(object_t *o, const char *name) {
    if (!o->symtab || !o->strtab) return NULL;

    if (o->hash) {
        uint32_t nbucket = o->hash[0];
        uint32_t nchain  = o->hash[1];
        if (!nbucket) return NULL;
        uint32_t *bucket = o->hash + 2;
        uint32_t *chain  = bucket + nbucket;
        uint32_t i = bucket[elf_hash(name) % nbucket];
        while (i != SHN_UNDEF && i < nchain) {
            sym_t *s = &o->symtab[i];
            if (s->st_shndx != SHN_UNDEF && ld_streq(o->strtab + s->st_name, name))
                return s;
            i = chain[i];
        }
        return NULL;
    }
    return NULL;
}

static sym_t *resolve_sym(const char *name, object_t *skip, object_t **def) {
    for (int i = 0; i < g_nobjs; i++) {
        if (&g_objs[i] == skip) continue;
        sym_t *s = lookup_in(&g_objs[i], name);
        if (s) { *def = &g_objs[i]; return s; }
    }
    return NULL;
}

static uintptr_t resolve(const char *name, object_t *skip) {
    object_t *d;
    sym_t *s = resolve_sym(name, skip, &d);
    return s ? d->base + s->st_value : 0;
}

static size_t align_up(size_t v, size_t a) {
    if (a < 1) a = 1;
    return (v + a - 1) & ~(a - 1);
}

static size_t tls_place(size_t used, const tls_mod_t *m) {
    size_t first = (size_t)(-m->image) & (m->align - 1);
    size_t need = used + m->memsz;
    return align_up(need > first ? need - first : 0, m->align) + first;
}

static size_t g_static_pending[MAX_TLS_MODS];
static size_t g_npending;

static void tls_register(object_t *o, const map_info_t *mi, int at_start) {
    o->tls_modid = 0;
    if (!mi->tls_memsz) return;
    if (g_tls.nmods >= MAX_TLS_MODS) ld_die("too many modules with thread-local storage", o->name);
    size_t id = ++g_tls.nmods;
    tls_mod_t *m = &g_tls_mods[id];
    m->image  = mi->tls_image;
    m->filesz = mi->tls_filesz;
    m->memsz  = mi->tls_memsz;
    m->align  = mi->tls_align ? mi->tls_align : 1;
    m->offset = 0;
    m->is_static = 0;
    o->tls_modid = id;
    if (at_start) {
        size_t off = tls_place(g_tls.static_used, m);
        m->offset = off;
        m->is_static = 1;
        g_tls.static_used = off;
        if (m->align > g_tls.static_align) g_tls.static_align = m->align;
    }
    g_tls.gen++;
}

static size_t tls_static_offset(object_t *o) {
    if (!o->tls_modid) ld_die("initial-exec TLS reference to a module without TLS", o->name);
    tls_mod_t *m = &g_tls_mods[o->tls_modid];
    if (__atomic_load_n(&m->is_static, __ATOMIC_ACQUIRE) == 1) return m->offset;
    size_t off = tls_place(g_tls.static_used, m);
    if (off > g_tls.static_size || m->align > g_tls.static_align)
        ld_die("no static TLS space left for", o->name);
    m->offset = off;
    int expect = 0;
    if (!__atomic_compare_exchange_n(&m->is_static, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        ld_die("thread-local storage is already dynamic, cannot use initial-exec TLS in", o->name);
    g_tls.static_used = off;
    g_static_pending[g_npending++] = o->tls_modid;
    g_tls.gen++;
    return off;
}

static void unresolved_call(void)
{
    ld_die("a function that no loaded library provides was called", NULL);
}

static void apply_relocations(object_t *o)
{
    rela_t  *rela = NULL, *jmprel = NULL;
    uint64_t relasz = 0, pltrelsz = 0;

    for (dyn_t *d = o->dyn; d && d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_RELA:      rela     = (rela_t *)(o->base + d->d_val); break;
            case DT_RELASZ:    relasz   = d->d_val; break;
            case DT_JMPREL:    jmprel   = (rela_t *)(o->base + d->d_val); break;
            case DT_PLTRELSZ:  pltrelsz = d->d_val; break;
            default: break;
        }
    }

    for (int pass = 0; pass < 2; pass++) {
        rela_t  *tbl = pass ? jmprel : rela;
        uint64_t sz  = pass ? pltrelsz : relasz;
        if (!tbl || !sz) continue;

        for (uint64_t k = 0; k < sz / sizeof(rela_t); k++) {
            rela_t   *r    = &tbl[k];
            uint32_t  type = ELF64_R_TYPE(r->r_info);
            uint32_t  si   = ELF64_R_SYM(r->r_info);
            uint64_t *slot = (uint64_t *)(o->base + r->r_offset);

            if (type == R_X86_64_RELATIVE) {
                *slot = o->base + (uint64_t)r->r_addend;
                continue;
            }
            if (type == R_X86_64_NONE) continue;
            if (type == R_X86_64_IRELATIVE) {
                *slot = ((uintptr_t (*)(void))(o->base + (uint64_t)r->r_addend))();
                continue;
            }
            if (type == R_X86_64_TLSDESC) ld_die("TLS descriptors are not supported, rebuild with -mtls-dialect=gnu", o->name);

            const char *nm = (o->symtab && o->strtab)
                           ? o->strtab + o->symtab[si].st_name : "";

            if (type == R_X86_64_DTPMOD64 || type == R_X86_64_DTPOFF64 || type == R_X86_64_TPOFF64) {
                object_t *def = o;
                uint64_t symoff = 0;
                if (si) {
                    sym_t *ts = resolve_sym(nm, NULL, &def);
                    if (!ts) {
                        sym_t *own = o->symtab ? &o->symtab[si] : NULL;
                        if (own && own->st_shndx != SHN_UNDEF) { ts = own; def = o; }
                    }
                    if (!ts) ld_die("undefined thread-local symbol", nm);
                    symoff = ts->st_value;
                }
                if (type == R_X86_64_DTPMOD64)      *slot = def->tls_modid;
                else if (type == R_X86_64_DTPOFF64) *slot = symoff + (uint64_t)r->r_addend;
                else *slot = symoff + (uint64_t)r->r_addend - tls_static_offset(def);
                continue;
            }

            uintptr_t val = resolve(nm, type == R_X86_64_COPY ? o : NULL);
            if (!val) {
                sym_t *own = (o->symtab && si) ? &o->symtab[si] : NULL;
                if (own && own->st_shndx != SHN_UNDEF) val = o->base + own->st_value;
            }
            if (!val && type == R_X86_64_JUMP_SLOT) {
                sym_t *own = (o->symtab && si) ? &o->symtab[si] : NULL;
                if (!own || (own->st_info >> 4) != STB_WEAK) val = (uintptr_t)unresolved_call;
            } else if (!val) {
                sym_t *own = (o->symtab && si) ? &o->symtab[si] : NULL;
                if (!own || (own->st_info >> 4) != STB_WEAK) ld_die("undefined symbol", nm);
            }

            switch (type) {
                case R_X86_64_COPY: {
                    sym_t *own = (o->symtab && si) ? &o->symtab[si] : NULL;
                    const unsigned char *src = (const unsigned char *)val;
                    unsigned char *dst = (unsigned char *)slot;
                    for (uint64_t b = 0; own && src && b < own->st_size; b++) dst[b] = src[b];
                    break;
                }
                case R_X86_64_64:
                    *slot = val + (uint64_t)r->r_addend;
                    break;
                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT:
                    *slot = val;
                    break;
                default:
                    ld_die("unsupported relocation in", o->name);
            }
        }
    }
    o->relocated = 1;
}

static long try_open(char *full, size_t cap, const char *dir, size_t dirlen, const char *name)
{
    size_t n = 0;
    for (size_t i = 0; i < dirlen && n + 1 < cap; i++) full[n++] = dir[i];
    if (n && full[n - 1] != '/' && n + 1 < cap) full[n++] = '/';
    for (const char *p = name; *p && n + 1 < cap; p++) full[n++] = *p;
    full[n] = 0;
    return sys3(SYS_OPEN, (long)full, O_RDONLY, 0);
}

static long search_list(char *full, size_t cap, const char *list, const char *name,
                        const char *origin, size_t originlen)
{
    while (list && *list) {
        const char *end = list;
        while (*end && *end != ':') end++;
        size_t len = (size_t)(end - list);
        if (len >= 7 && list[0] == '$' && list[1] == 'O' && list[2] == 'R' && list[3] == 'I' &&
            list[4] == 'G' && list[5] == 'I' && list[6] == 'N') {
            if (origin) {
                char dir[256];
                size_t n = 0;
                for (size_t i = 0; i < originlen && n + 1 < sizeof dir; i++) dir[n++] = origin[i];
                for (size_t i = 7; i < len && n + 1 < sizeof dir; i++) dir[n++] = list[i];
                long fd = try_open(full, cap, dir, n, name);
                if (fd >= 0) return fd;
            }
        } else if (len) {
            long fd = try_open(full, cap, list, len, name);
            if (fd >= 0) return fd;
        }
        list = *end ? end + 1 : end;
    }
    return -1;
}

static long find_library(char *full, size_t cap, const char *name, const object_t *req)
{
    int has_slash = 0;
    for (const char *p = name; *p; p++) if (*p == '/') has_slash = 1;
    if (has_slash) {
        ld_copy(full, name, cap);
        return sys3(SYS_OPEN, (long)full, O_RDONLY, 0);
    }

    long fd = search_list(full, cap, g_library_path, name, NULL, 0);
    if (fd >= 0) return fd;

    if (req && req->dyn && req->strtab) {
        const char *runpath = NULL, *rpath = NULL;
        for (dyn_t *d = req->dyn; d->d_tag != DT_NULL; d++) {
            if (d->d_tag == DT_RUNPATH) runpath = req->strtab + d->d_val;
            else if (d->d_tag == DT_RPATH) rpath = req->strtab + d->d_val;
        }
        const char *origin = NULL;
        size_t originlen = 0;
        if (req->path) {
            for (const char *p = req->path; *p; p++) if (*p == '/') originlen = (size_t)(p - req->path);
            if (originlen) origin = req->path;
        }
        fd = search_list(full, cap, runpath ? runpath : rpath, name, origin, originlen);
        if (fd >= 0) return fd;
    }

    return search_list(full, cap, "/lib:/usr/lib", name, NULL, 0);
}

static uintptr_t g_map_lo, g_map_hi;

static uintptr_t map_library(const char *path, dyn_t **dyn_out, const object_t *req, char *full, size_t cap,
                             map_info_t *mi)
{
    mi->phdr = NULL;
    mi->phnum = 0;
    mi->tls_image = 0;
    mi->tls_filesz = mi->tls_memsz = mi->tls_align = 0;
    long fd = find_library(full, cap, path, req);
    if (fd < 0) { g_fail = "cannot find library"; return 0; }

    ehdr_t eh;
    if (sys3(SYS_READ, fd, (long)&eh, sizeof eh) != (long)sizeof eh ||
        eh.e_ident[0] != 0x7F || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' || eh.e_ident[3] != 'F')
        { g_fail = "not an ELF file"; sys1(SYS_CLOSE, fd); return 0; }

    uintptr_t lo = ~(uintptr_t)0, hi = 0;
    phdr_t ph;
    for (int i = 0; i < eh.e_phnum; i++) {
        sys3(SYS_SEEK, fd, (long)(eh.e_phoff + (uint64_t)eh.e_phentsize * i), 0);
        if (sys3(SYS_READ, fd, (long)&ph, sizeof ph) != (long)sizeof ph)
            { g_fail = "short phdr in"; sys1(SYS_CLOSE, fd); return 0; }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_vaddr < lo) lo = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > hi) hi = ph.p_vaddr + ph.p_memsz;
    }
    if (lo > hi) { g_fail = "no loadable segments in"; sys1(SYS_CLOSE, fd); return 0; }

    lo &= ~(uintptr_t)0xFFF;
    size_t span = (hi - lo + 0xFFF) & ~(uintptr_t)0xFFF;

    long area = sys6(SYS_MMAP, 0, (long)span, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area <= 0) { g_fail = "out of memory mapping"; sys1(SYS_CLOSE, fd); return 0; }

    uintptr_t base = (uintptr_t)area - lo;
    g_map_lo = (uintptr_t)area;
    g_map_hi = (uintptr_t)area + span;
    uint64_t phbytes = (uint64_t)eh.e_phentsize * eh.e_phnum;

    for (int i = 0; i < eh.e_phnum; i++) {
        sys3(SYS_SEEK, fd, (long)(eh.e_phoff + (uint64_t)eh.e_phentsize * i), 0);
        if (sys3(SYS_READ, fd, (long)&ph, sizeof ph) != (long)sizeof ph) break;
        if (ph.p_type == PT_DYNAMIC) *dyn_out = (dyn_t *)(base + ph.p_vaddr);
        if (ph.p_type == PT_TLS) {
            mi->tls_image  = base + ph.p_vaddr;
            mi->tls_filesz = ph.p_filesz;
            mi->tls_memsz  = ph.p_memsz;
            mi->tls_align  = ph.p_align;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (!mi->phdr && eh.e_phoff >= ph.p_offset && eh.e_phoff + phbytes <= ph.p_offset + ph.p_filesz) {
            mi->phdr = (const phdr_t *)(base + ph.p_vaddr + (eh.e_phoff - ph.p_offset));
            mi->phnum = eh.e_phnum;
        }
        if (ph.p_filesz) {
            sys3(SYS_SEEK, fd, (long)ph.p_offset, 0);
            uint64_t done = 0;
            while (done < ph.p_filesz) {
                long got = sys3(SYS_READ, fd, (long)(base + ph.p_vaddr + done), (long)(ph.p_filesz - done));
                if (got <= 0) break;
                done += (uint64_t)got;
            }
            if (done != ph.p_filesz) { g_fail = "short segment read in"; sys1(SYS_CLOSE, fd); return 0; }
        }
        char *zero = (char *)(base + ph.p_vaddr + ph.p_filesz);
        for (uint64_t z = ph.p_filesz; z < ph.p_memsz; z++) *zero++ = 0;
    }

    if (!mi->phdr && eh.e_phentsize == sizeof(phdr_t)) {
        long copy = sys6(SYS_MMAP, 0, (long)((phbytes + 0xFFF) & ~0xFFFul), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (copy > 0) {
            sys3(SYS_SEEK, fd, (long)eh.e_phoff, 0);
            if (sys3(SYS_READ, fd, copy, (long)phbytes) == (long)phbytes) {
                mi->phdr = (const phdr_t *)copy;
                mi->phnum = eh.e_phnum;
            }
        }
    }

    sys1(SYS_CLOSE, fd);
    return base;
}

static void load_needed(object_t *o, int at_start)
{
    for (dyn_t *d = o->dyn; d && d->d_tag != DT_NULL; d++) {
        if (d->d_tag != DT_NEEDED) continue;
        const char *name = o->strtab + d->d_val;

        int already = -1;
        for (int i = 0; i < g_nobjs; i++)
            if (g_objs[i].name && ld_streq(g_objs[i].name, name)) { already = i; break; }
        if (already >= 0) {
            if (o->nneeded < MAX_NEEDED) o->needed[o->nneeded++] = (short)already;
            continue;
        }

        if (g_nobjs >= MAX_OBJECTS) ld_die("too many libraries", name);

        static char names[MAX_OBJECTS][64];
        static char paths[MAX_OBJECTS][256];
        ld_copy(names[g_nobjs], name, sizeof names[0]);

        dyn_t *ndyn = NULL;
        map_info_t mi;
        uintptr_t base = map_library(names[g_nobjs], &ndyn, o, paths[g_nobjs], sizeof paths[0], &mi);
        if (!base) ld_die(g_fail, names[g_nobjs]);
        if (!ndyn) ld_die("library has no dynamic section", names[g_nobjs]);

        if (o->nneeded < MAX_NEEDED) o->needed[o->nneeded++] = (short)g_nobjs;
        object_t *no = &g_objs[g_nobjs++];
        no->name = names[g_nobjs - 1];
        no->path = paths[g_nobjs - 1];
        no->base = base;
        no->dyn  = ndyn;
        no->map_start = g_map_lo;
        no->map_end   = g_map_hi;
        no->relocated = 0;
        no->phdr  = mi.phdr;
        no->phnum = mi.phnum;
        tls_register(no, &mi, at_start);
        scan_dynamic(no);
        load_needed(no, at_start);
    }
}

typedef struct {
    const char *fname;
    void       *fbase;
    const char *sname;
    void       *saddr;
} dl_info_t;

typedef struct {
    uintptr_t     addr;
    const char   *name;
    const phdr_t *phdr;
    uint16_t      phnum;
    unsigned long long adds, subs;
    size_t        tls_modid;
    void         *tls_data;
} dl_phdr_info_t;

typedef int (*phdr_cb_t)(dl_phdr_info_t *info, size_t size, void *data);

typedef struct {
    void *(*open)(const char *path, int flags);
    void *(*sym)(void *handle, const char *name, const void *caller);
    int   (*close)(void *handle);
    void  (*init)(int argc, char **argv, char **envp);
    int   (*addr)(const void *addr, dl_info_t *info);
    const tls_layout_t *(*tls)(void);
    int   (*iterate)(phdr_cb_t cb, void *data);
} dl_ops_t;

static unsigned long long g_adds;

typedef void (*init_fn_t)(int, char **, char **);

static int    g_argc;
static char **g_argv;
static char **g_envp;

static void run_init(object_t *o)
{
    if (o->inited) return;
    o->inited = 1;
    uintptr_t init = 0, arr = 0;
    uint64_t arrsz = 0;
    for (dyn_t *d = o->dyn; d && d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_INIT)            init  = o->base + d->d_val;
        else if (d->d_tag == DT_INIT_ARRAY) arr   = o->base + d->d_val;
        else if (d->d_tag == DT_INIT_ARRAYSZ) arrsz = d->d_val;
    }
    if (init) ((init_fn_t)init)(g_argc, g_argv, g_envp);
    for (uint64_t i = 0; arr && i < arrsz / sizeof(uintptr_t); i++) {
        uintptr_t fn = ((uintptr_t *)arr)[i];
        if (fn && fn != (uintptr_t)-1) ((init_fn_t)fn)(g_argc, g_argv, g_envp);
    }
}

static void ld_init_libs(int argc, char **argv, char **envp)
{
    g_argc = argc;
    g_argv = argv;
    g_envp = envp;
    for (int i = g_nobjs - 1; i >= 1; i--) run_init(&g_objs[i]);
}

static void *ld_dlopen(const char *path, int flags)
{
    (void)flags;
    if (!path) return &g_objs[0];

    for (int i = 0; i < g_nobjs; i++)
        if (g_objs[i].name && ld_streq(g_objs[i].name, path)) return &g_objs[i];

    if (g_nobjs >= MAX_OBJECTS) return 0;

    int first = g_nobjs;
    static char dl_paths[MAX_OBJECTS][256];
    static char dl_names[MAX_OBJECTS][256];
    dyn_t *dyn = NULL;
    map_info_t mi;
    uintptr_t base = map_library(path, &dyn, &g_objs[0], dl_paths[g_nobjs], sizeof dl_paths[0], &mi);
    if (!base || !dyn) return 0;
    object_t *o = &g_objs[g_nobjs];
    ld_copy(dl_names[g_nobjs], path, sizeof dl_names[0]);
    o->name = dl_names[g_nobjs];
    o->path = dl_paths[g_nobjs];
    g_nobjs++;
    o->base = base;
    o->dyn  = dyn;
    o->map_start = g_map_lo;
    o->map_end   = g_map_hi;
    o->phdr  = mi.phdr;
    o->phnum = mi.phnum;
    tls_register(o, &mi, 0);
    scan_dynamic(o);
    load_needed(o, 0);
    g_adds += (unsigned long long)(g_nobjs - first);

    g_npending = 0;
    for (int i = g_nobjs - 1; i >= first; i--) apply_relocations(&g_objs[i]);
    if (g_npending) {
        void (*hook)(size_t) = (void (*)(size_t))resolve("__cervus_tls_static_init", NULL);
        for (size_t i = 0; i < g_npending; i++) if (hook) hook(g_static_pending[i]);
        g_npending = 0;
    }
    for (int i = g_nobjs - 1; i >= first; i--) run_init(&g_objs[i]);
    return o;
}

static int object_of(uintptr_t a)
{
    for (int i = 0; i < g_nobjs; i++)
        if (a >= g_objs[i].map_start && a < g_objs[i].map_end) return i;
    return -1;
}

static void *sym_addr(object_t *o, sym_t *s)
{
    if ((s->st_info & 0xf) != STT_TLS) return (void *)(o->base + s->st_value);
    void *(*gta)(size_t *) = (void *(*)(size_t *))resolve("__tls_get_addr", NULL);
    if (!gta || !o->tls_modid) return 0;
    size_t ti[2] = { o->tls_modid, s->st_value };
    return gta(ti);
}

static void *ld_dlsym(void *handle, const char *name, const void *caller)
{
    if (!name) return 0;
    if (!handle || handle == (void *)&g_objs[0]) {
        object_t *d;
        sym_t *s = resolve_sym(name, NULL, &d);
        return s ? sym_addr(d, s) : 0;
    }
    if (handle == (void *)-1) {
        int from = object_of((uintptr_t)caller);
        for (int i = from + 1; i < g_nobjs; i++) {
            sym_t *s = lookup_in(&g_objs[i], name);
            if (s) return sym_addr(&g_objs[i], s);
        }
        return 0;
    }
    object_t *root = handle;
    if (root < &g_objs[0] || root >= &g_objs[g_nobjs]) return 0;
    static unsigned char seen[MAX_OBJECTS];
    static short queue[MAX_OBJECTS];
    for (int i = 0; i < g_nobjs; i++) seen[i] = 0;
    int head = 0, tail = 0;
    queue[tail++] = (short)(root - g_objs);
    seen[root - g_objs] = 1;
    while (head < tail) {
        object_t *o = &g_objs[queue[head++]];
        sym_t *s = lookup_in(o, name);
        if (s) return sym_addr(o, s);
        for (int k = 0; k < o->nneeded; k++) {
            int d = o->needed[k];
            if (!seen[d]) { seen[d] = 1; queue[tail++] = (short)d; }
        }
    }
    return 0;
}

static int ld_dlclose(void *handle)
{
    (void)handle;
    return 0;
}

static const char *g_prog_path;

static int ld_dladdr(const void *addr, dl_info_t *info)
{
    uintptr_t a = (uintptr_t)addr;
    for (int i = 0; i < g_nobjs; i++) {
        object_t *o = &g_objs[i];
        if (a < o->map_start || a >= o->map_end) continue;
        info->fname = i == 0 ? (g_prog_path ? g_prog_path : "") : o->path;
        info->fbase = (void *)o->map_start;
        info->sname = 0;
        info->saddr = 0;
        uint32_t nsym = o->hash ? o->hash[1] : 0;
        uintptr_t best = 0;
        for (uint32_t k = 1; o->symtab && o->strtab && k < nsym; k++) {
            sym_t *s = &o->symtab[k];
            if (s->st_shndx == SHN_UNDEF || !s->st_value) continue;
            uint8_t type = s->st_info & 0xf;
            if (type != 1 && type != 2) continue;
            uintptr_t sa = o->base + s->st_value;
            if (sa > a) continue;
            if (s->st_size && a >= sa + s->st_size) continue;
            if (sa >= best) {
                best = sa;
                info->sname = o->strtab + s->st_name;
                info->saddr = (void *)sa;
            }
        }
        return 1;
    }
    return 0;
}

static const tls_layout_t *ld_tls(void)
{
    return &g_tls;
}

static int ld_iterate(phdr_cb_t cb, void *data)
{
    for (int i = 0; i < g_nobjs; i++) {
        object_t *o = &g_objs[i];
        dl_phdr_info_t info;
        info.addr = o->base;
        info.name = i == 0 ? "" : o->path;
        info.phdr = o->phdr;
        info.phnum = o->phnum;
        info.adds = g_adds;
        info.subs = 0;
        info.tls_modid = o->tls_modid;
        info.tls_data = 0;
        int r = cb(&info, sizeof info, data);
        if (r) return r;
    }
    return 0;
}

static dl_ops_t g_dl_ops = { ld_dlopen, ld_dlsym, ld_dlclose, ld_init_libs, ld_dladdr, ld_tls, ld_iterate };

uintptr_t ld_start_c(uint64_t *sp)
{
    uint64_t argc = sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;

    for (char **e = envp; *e; e++) {
        const char *v = *e;
        if (v[0] == 'L' && v[1] == 'D' && v[2] == '_' && v[3] == 'L' && v[4] == 'I' &&
            v[5] == 'B' && v[6] == 'R' && v[7] == 'A' && v[8] == 'R' && v[9] == 'Y' &&
            v[10] == '_' && v[11] == 'P' && v[12] == 'A' && v[13] == 'T' && v[14] == 'H' &&
            v[15] == '=')
            g_library_path = v + 16;
    }

    uint64_t *auxv = (uint64_t *)envp;
    while (*auxv) auxv++;
    auxv++;

    uintptr_t phdr = 0, entry = 0, interp_base = 0;
    const char *execfn = 0;
    uint64_t phnum = 0, phent = 56;

    for (uint64_t *a = auxv; a[0] != AT_NULL; a += 2) {
        switch (a[0]) {
            case AT_PHDR:  phdr  = (uintptr_t)a[1]; break;
            case AT_PHNUM: phnum = a[1]; break;
            case AT_PHENT: phent = a[1]; break;
            case AT_ENTRY: entry = (uintptr_t)a[1]; break;
            case AT_BASE:  interp_base = (uintptr_t)a[1]; break;
            case 31:       execfn = (const char *)a[1]; break;
            default: break;
        }
    }

    if (!phdr || !entry) ld_die("the kernel gave no program headers", NULL);

    uintptr_t exec_base = 0;
    dyn_t *exec_dyn = NULL;
    for (uint64_t i = 0; i < phnum; i++) {
        phdr_t *p = (phdr_t *)(phdr + i * phent);
        if (p->p_type == PT_PHDR) exec_base = phdr - p->p_vaddr;
    }
    for (uint64_t i = 0; i < phnum; i++) {
        phdr_t *p = (phdr_t *)(phdr + i * phent);
        if (p->p_type == PT_DYNAMIC) exec_dyn = (dyn_t *)(exec_base + p->p_vaddr);
    }

    if (!exec_dyn) return entry;

    map_info_t emi;
    emi.phdr = (const phdr_t *)phdr;
    emi.phnum = (uint16_t)phnum;
    emi.tls_image = 0;
    emi.tls_filesz = emi.tls_memsz = emi.tls_align = 0;
    for (uint64_t i = 0; i < phnum; i++) {
        phdr_t *p = (phdr_t *)(phdr + i * phent);
        if (p->p_type != PT_TLS) continue;
        emi.tls_image  = exec_base + p->p_vaddr;
        emi.tls_filesz = p->p_filesz;
        emi.tls_memsz  = p->p_memsz;
        emi.tls_align  = p->p_align;
    }

    if (exec_dyn) {
        uintptr_t plo = ~(uintptr_t)0, phi = 0;
        for (uint64_t i = 0; i < phnum; i++) {
            phdr_t *p = (phdr_t *)(phdr + i * phent);
            if (p->p_type != PT_LOAD) continue;
            if (exec_base + p->p_vaddr < plo) plo = exec_base + p->p_vaddr;
            if (exec_base + p->p_vaddr + p->p_memsz > phi) phi = exec_base + p->p_vaddr + p->p_memsz;
        }
        g_objs[0].map_start = plo & ~(uintptr_t)0xFFF;
        g_objs[0].map_end   = phi;
        g_prog_path = execfn ? execfn : (argc ? argv[0] : 0);
        g_objs[0].name = "the program";
        g_objs[0].base = exec_base;
        g_objs[0].dyn  = exec_dyn;
        g_nobjs = 1;
        g_objs[0].inited = 1;
        g_objs[0].phdr = emi.phdr;
        g_objs[0].phnum = emi.phnum;
        tls_register(&g_objs[0], &emi, 1);
        scan_dynamic(&g_objs[0]);
        load_needed(&g_objs[0], 1);
        g_adds = (unsigned long long)g_nobjs;
        g_tls.static_size = align_up(g_tls.static_used, 64) + TLS_SURPLUS;
        if (g_tls.static_align < 64) g_tls.static_align = 64;

        for (int i = g_nobjs - 1; i >= 0; i--) apply_relocations(&g_objs[i]);

        uintptr_t slot = resolve("__cervus_dl_ops", 0);
        if (slot) *(dl_ops_t **)slot = &g_dl_ops;
    }

    (void)interp_base;
    return entry;
}
