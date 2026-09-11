#include <stdint.h>
#include <stddef.h>

#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_BASE    7
#define AT_ENTRY   9

#define PT_LOAD    1
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

#define MAX_OBJECTS 24

typedef struct {
    const char *name;
    uintptr_t   base;
    dyn_t      *dyn;
    const char *strtab;
    sym_t      *symtab;
    uint32_t   *hash;
    int         relocated;
} object_t;

static object_t g_objs[MAX_OBJECTS];
static int      g_nobjs;

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
            if (!val) ld_die("undefined symbol", nm);

            switch (type) {
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

static uintptr_t map_library(const char *path, dyn_t **dyn_out)
{
    static const char *const DIRS[] = { "/lib/", "/usr/lib/", "" };
    char full[256];
    long fd = -1;

    for (size_t d = 0; d < sizeof DIRS / sizeof DIRS[0]; d++) {
        size_t n = 0;
        const char *p = DIRS[d];
        while (*p && n + 1 < sizeof full) full[n++] = *p++;
        p = path;
        while (*p && n + 1 < sizeof full) full[n++] = *p++;
        full[n] = 0;
        fd = sys3(SYS_OPEN, (long)full, O_RDONLY, 0);
        if (fd >= 0) break;
    }
    if (fd < 0) ld_die("cannot find library", path);

    ehdr_t eh;
    if (sys3(SYS_READ, fd, (long)&eh, sizeof eh) != (long)sizeof eh)
        ld_die("short read on", path);

    uintptr_t lo = ~(uintptr_t)0, hi = 0;
    phdr_t ph;
    for (int i = 0; i < eh.e_phnum; i++) {
        sys3(SYS_SEEK, fd, (long)(eh.e_phoff + (uint64_t)eh.e_phentsize * i), 0);
        if (sys3(SYS_READ, fd, (long)&ph, sizeof ph) != (long)sizeof ph)
            ld_die("short phdr in", path);
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_vaddr < lo) lo = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > hi) hi = ph.p_vaddr + ph.p_memsz;
    }
    if (lo > hi) ld_die("no loadable segments in", path);

    lo &= ~(uintptr_t)0xFFF;
    size_t span = (hi - lo + 0xFFF) & ~(uintptr_t)0xFFF;

    long area = sys6(SYS_MMAP, 0, (long)span, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area <= 0) ld_die("out of memory mapping", path);

    uintptr_t base = (uintptr_t)area - lo;

    for (int i = 0; i < eh.e_phnum; i++) {
        sys3(SYS_SEEK, fd, (long)(eh.e_phoff + (uint64_t)eh.e_phentsize * i), 0);
        if (sys3(SYS_READ, fd, (long)&ph, sizeof ph) != (long)sizeof ph) break;
        if (ph.p_type == PT_DYNAMIC) *dyn_out = (dyn_t *)(base + ph.p_vaddr);
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_filesz) {
            sys3(SYS_SEEK, fd, (long)ph.p_offset, 0);
            long got = sys3(SYS_READ, fd, (long)(base + ph.p_vaddr), (long)ph.p_filesz);
            if (got != (long)ph.p_filesz) ld_die("short segment read in", path);
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
        ld_copy(names[g_nobjs], name, sizeof names[0]);

        dyn_t *ndyn = NULL;
        uintptr_t base = map_library(names[g_nobjs], &ndyn);
        if (!ndyn) ld_die("library has no dynamic section", names[g_nobjs]);

        object_t *no = &g_objs[g_nobjs++];
        no->name = names[g_nobjs - 1];
        no->base = base;
        no->dyn  = ndyn;
        no->relocated = 0;
        scan_dynamic(no);
        load_needed(no);
    }
}

uintptr_t ld_start_c(uint64_t *sp)
{
    uint64_t argc = sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;

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
        if (p->p_type == PT_DYNAMIC) exec_dyn = (dyn_t *)p->p_vaddr;
    }

    if (!exec_dyn) return entry;

    if (exec_dyn) {
        g_objs[0].name = "the program";
        g_objs[0].base = exec_base;
        g_objs[0].dyn  = exec_dyn;
        g_nobjs = 1;
        scan_dynamic(&g_objs[0]);
        load_needed(&g_objs[0]);

        for (int i = g_nobjs - 1; i >= 0; i--) apply_relocations(&g_objs[i]);
    }

    (void)interp_base;
    return entry;
}
