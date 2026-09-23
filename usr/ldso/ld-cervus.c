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

#define SHN_UNDEF 0
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

#define MAX_OBJECTS 64

typedef struct {
    const char *name;
    const char *path;
    uintptr_t   base;
    dyn_t      *dyn;
    const char *strtab;
    sym_t      *symtab;
    uint32_t   *hash;
    int         relocated;
    int         inited;
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

static uintptr_t resolve(const char *name, object_t *skip) {
    for (int i = 0; i < g_nobjs; i++) {
        if (&g_objs[i] == skip) continue;
        sym_t *s = lookup_in(&g_objs[i], name);
        if (s) return g_objs[i].base + s->st_value;
    }
    for (int i = 0; i < g_nobjs; i++) {
        if (&g_objs[i] != skip) continue;
        sym_t *s = lookup_in(&g_objs[i], name);
        if (s) return g_objs[i].base + s->st_value;
    }
    return 0;
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

            const char *nm = (o->symtab && o->strtab)
                           ? o->strtab + o->symtab[si].st_name : "";
            uintptr_t val = resolve(nm, o);
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

static uintptr_t map_library(const char *path, dyn_t **dyn_out, const object_t *req, char *full, size_t cap)
{
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

    for (int i = 0; i < eh.e_phnum; i++) {
        sys3(SYS_SEEK, fd, (long)(eh.e_phoff + (uint64_t)eh.e_phentsize * i), 0);
        if (sys3(SYS_READ, fd, (long)&ph, sizeof ph) != (long)sizeof ph) break;
        if (ph.p_type == PT_DYNAMIC) *dyn_out = (dyn_t *)(base + ph.p_vaddr);
        if (ph.p_type != PT_LOAD) continue;
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

    sys1(SYS_CLOSE, fd);
    return base;
}

static void load_needed(object_t *o)
{
    for (dyn_t *d = o->dyn; d && d->d_tag != DT_NULL; d++) {
        if (d->d_tag != DT_NEEDED) continue;
        const char *name = o->strtab + d->d_val;

        int already = 0;
        for (int i = 0; i < g_nobjs; i++)
            if (g_objs[i].name && ld_streq(g_objs[i].name, name)) { already = 1; break; }
        if (already) continue;

        if (g_nobjs >= MAX_OBJECTS) ld_die("too many libraries", name);

        static char names[MAX_OBJECTS][64];
        static char paths[MAX_OBJECTS][256];
        ld_copy(names[g_nobjs], name, sizeof names[0]);

        dyn_t *ndyn = NULL;
        uintptr_t base = map_library(names[g_nobjs], &ndyn, o, paths[g_nobjs], sizeof paths[0]);
        if (!base) ld_die(g_fail, names[g_nobjs]);
        if (!ndyn) ld_die("library has no dynamic section", names[g_nobjs]);

        object_t *no = &g_objs[g_nobjs++];
        no->name = names[g_nobjs - 1];
        no->path = paths[g_nobjs - 1];
        no->base = base;
        no->dyn  = ndyn;
        no->relocated = 0;
        scan_dynamic(no);
        load_needed(no);
    }
}

typedef struct {
    void *(*open)(const char *path, int flags);
    void *(*sym)(void *handle, const char *name);
    int   (*close)(void *handle);
    void  (*init)(int argc, char **argv, char **envp);
} dl_ops_t;

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
    dyn_t *dyn = NULL;
    uintptr_t base = map_library(path, &dyn, &g_objs[0], dl_paths[g_nobjs], sizeof dl_paths[0]);
    if (!base || !dyn) return 0;
    object_t *o = &g_objs[g_nobjs];
    o->name = path;
    o->path = dl_paths[g_nobjs];
    g_nobjs++;
    o->base = base;
    o->dyn  = dyn;
    scan_dynamic(o);
    load_needed(o);

    for (int i = g_nobjs - 1; i >= first; i--) apply_relocations(&g_objs[i]);
    for (int i = g_nobjs - 1; i >= first; i--) run_init(&g_objs[i]);
    return o;
}

static void *ld_dlsym(void *handle, const char *name)
{
    if (!name) return 0;
    if (!handle) {
        uintptr_t a = resolve(name, 0);
        return (void *)a;
    }
    object_t *o = handle;
    sym_t *s = lookup_in(o, name);
    if (!s) return 0;
    return (void *)(o->base + s->st_value);
}

static int ld_dlclose(void *handle)
{
    (void)handle;
    return 0;
}

static dl_ops_t g_dl_ops = { ld_dlopen, ld_dlsym, ld_dlclose, ld_init_libs };

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
    uint64_t phnum = 0, phent = 56;

    for (uint64_t *a = auxv; a[0] != AT_NULL; a += 2) {
        switch (a[0]) {
            case AT_PHDR:  phdr  = (uintptr_t)a[1]; break;
            case AT_PHNUM: phnum = a[1]; break;
            case AT_PHENT: phent = a[1]; break;
            case AT_ENTRY: entry = (uintptr_t)a[1]; break;
            case AT_BASE:  interp_base = (uintptr_t)a[1]; break;
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

    if (exec_dyn) {
        g_objs[0].name = "the program";
        g_objs[0].base = exec_base;
        g_objs[0].dyn  = exec_dyn;
        g_nobjs = 1;
        g_objs[0].inited = 1;
        scan_dynamic(&g_objs[0]);
        load_needed(&g_objs[0]);

        for (int i = g_nobjs - 1; i >= 0; i--) apply_relocations(&g_objs[i]);

        uintptr_t slot = resolve("__cervus_dl_ops", 0);
        if (slot) *(dl_ops_t **)slot = &g_dl_ops;
    }

    (void)interp_base;
    return entry;
}
