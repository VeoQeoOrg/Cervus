#include "../../include/elf/elf.h"
#include "../../include/memory/vmm.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/paging.h"
#include "../../include/io/serial.h"
#include "../../include/fs/vfs.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    const uint8_t *buf;
    vnode_t       *vn;
    size_t         size;
} elf_source_t;

static int src_read(const elf_source_t *src, void *dst, uint64_t off, size_t len) {
    if (len == 0) return 0;
    if (off > src->size || off + len > src->size) return -1;
    if (src->buf) {
        memcpy(dst, src->buf + off, len);
        return 0;
    }
    if (src->vn && src->vn->ops && src->vn->ops->read) {
        size_t done = 0;
        uint8_t *d = dst;
        while (done < len) {
            int64_t r = src->vn->ops->read(src->vn, d + done, len - done, off + done);
            if (r <= 0) return -1;
            done += (size_t)r;
        }
        return 0;
    }
    return -1;
}

#define ELF_PIE_BASE        0x0000000000400000ULL
#define ELF_INTERP_BASE     0x00007F0000000000ULL
#define ELF_DEFAULT_STACK   (256 * 1024)

#ifndef SHF_WRITE
#define SHF_WRITE           0x1
#endif
#ifndef SHF_ALLOC
#define SHF_ALLOC           0x2
#endif
#ifndef SHF_EXECINSTR
#define SHF_EXECINSTR       0x4
#endif

static inline uintptr_t page_align_down(uintptr_t addr) {
    return addr & ~(uintptr_t)(PAGE_SIZE - 1);
}
static inline uintptr_t page_align_up(uintptr_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
}

static uint32_t pf_to_vm(uint32_t pf) {
    uint32_t f = 0;
    if (pf & (PF_R | PF_X)) f |= VM_READ;
    if (pf & PF_W) f |= VM_WRITE;
    if (pf & PF_X) f |= VM_EXEC;
    return f;
}

static elf_error_t elf_validate(const elf64_ehdr_t* hdr, size_t size) {
    if (size < sizeof(elf64_ehdr_t))          return ELF_ERR_TOO_SMALL;

    uint32_t magic;
    memcpy(&magic, hdr->e_ident, 4);
    if (magic != ELF_MAGIC)                   return ELF_ERR_BAD_MAGIC;
    if (hdr->e_ident[EI_CLASS]   != ELFCLASS64)  return ELF_ERR_NOT_64;
    if (hdr->e_ident[EI_DATA]    != ELFDATA2LSB) return ELF_ERR_NOT_LE;
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT ||
        hdr->e_version           != EV_CURRENT)  return ELF_ERR_BAD_VERSION;
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return ELF_ERR_NOT_EXEC;
    if (hdr->e_machine != EM_X86_64)          return ELF_ERR_WRONG_ARCH;
    if (hdr->e_phnum == 0)                    return ELF_ERR_NO_LOAD;

    uint64_t pht_end = (uint64_t)hdr->e_phoff +
                       (uint64_t)hdr->e_phentsize * hdr->e_phnum;
    if (pht_end > size)                       return ELF_ERR_TOO_SMALL;

    if (hdr->e_shnum && hdr->e_shoff) {
        uint64_t sht_end = (uint64_t)hdr->e_shoff +
                           (uint64_t)hdr->e_shentsize * hdr->e_shnum;
        if (sht_end > size)                   return ELF_ERR_TOO_SMALL;
    }
    return ELF_OK;
}

static int copy_from_source(mm_t *mm, const elf_source_t *src, uintptr_t dst,
                            uint64_t off, size_t len) {
    uint8_t *tmp = kmalloc(PAGE_SIZE);
    if (!tmp) return -1;
    while (len) {
        size_t chunk = len < PAGE_SIZE ? len : PAGE_SIZE;
        if (src_read(src, tmp, off, chunk) < 0 || mm_copy_to(mm, dst, tmp, chunk) < 0) {
            kfree(tmp);
            return -1;
        }
        dst += chunk; off += chunk; len -= chunk;
    }
    kfree(tmp);
    return 0;
}

static int zero_user(mm_t *mm, uintptr_t dst, size_t len) {
    static const uint8_t zeros[256];
    while (len) {
        size_t chunk = len < sizeof zeros ? len : sizeof zeros;
        if (mm_copy_to(mm, dst, zeros, chunk) < 0) return -1;
        dst += chunk; len -= chunk;
    }
    return 0;
}

#define EAGER_MAX 16

typedef struct { uintptr_t start, end; uint32_t prot; } eager_iv_t;

static elf_error_t load_all_eager(mm_t *mm, const elf_source_t *src,
                                  const elf64_ehdr_t *eh, uintptr_t bias) {
    eager_iv_t iv[EAGER_MAX];
    int n = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(src, &ph, eh->e_phoff + (uint64_t)eh->e_phentsize * i, sizeof ph) < 0)
            return ELF_ERR_TOO_SMALL;
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        if (ph.p_filesz > ph.p_memsz) return ELF_ERR_TOO_SMALL;
        uintptr_t s0 = page_align_down(ph.p_vaddr + bias);
        uintptr_t e0 = page_align_up(ph.p_vaddr + bias + ph.p_memsz);
        uint32_t pr = pf_to_vm(ph.p_flags);
        for (uintptr_t p = s0; p < e0; p += PAGE_SIZE) {
            int k;
            for (k = 0; k < n; k++)
                if (p >= iv[k].start && p < iv[k].end) break;
            if (k < n) {
                if ((iv[k].prot | pr) != iv[k].prot) {
                    if (iv[k].start == p && iv[k].end == p + PAGE_SIZE) { iv[k].prot |= pr; continue; }
                    if (n + 2 > EAGER_MAX) return ELF_ERR_NO_MEM;
                    eager_iv_t hi = { p + PAGE_SIZE, iv[k].end, iv[k].prot };
                    iv[k].end = p;
                    iv[n++] = (eager_iv_t){ p, p + PAGE_SIZE, iv[k].prot | pr };
                    if (hi.start < hi.end) iv[n++] = hi;
                }
                continue;
            }
            if (n && iv[n - 1].end == p && iv[n - 1].prot == pr) { iv[n - 1].end = p + PAGE_SIZE; continue; }
            if (n >= EAGER_MAX) return ELF_ERR_NO_MEM;
            iv[n++] = (eager_iv_t){ p, p + PAGE_SIZE, pr };
        }
    }
    for (int k = 0; k < n; k++) {
        if (iv[k].start >= iv[k].end) continue;
        if (mm_map_anon(mm, iv[k].start, iv[k].end - iv[k].start, iv[k].prot | VM_READ | VM_WRITE, true) < 0)
            return ELF_ERR_NO_MEM;
    }
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(src, &ph, eh->e_phoff + (uint64_t)eh->e_phentsize * i, sizeof ph) < 0)
            return ELF_ERR_TOO_SMALL;
        if (ph.p_type != PT_LOAD || ph.p_filesz == 0) continue;
        if (ph.p_offset + ph.p_filesz > src->size) return ELF_ERR_TOO_SMALL;
        if (copy_from_source(mm, src, ph.p_vaddr + bias, ph.p_offset, (size_t)ph.p_filesz) < 0)
            return ELF_ERR_TOO_SMALL;
    }
    for (int k = 0; k < n; k++)
        if (iv[k].start < iv[k].end && !(iv[k].prot & VM_WRITE))
            mm_protect(mm, iv[k].start, iv[k].end - iv[k].start, iv[k].prot);
    return ELF_OK;
}

static elf_error_t load_segment(mm_t *mm, const elf_source_t *src,
                                const elf64_phdr_t *ph, uintptr_t bias) {
    if (ph->p_memsz == 0) return ELF_OK;
    if (ph->p_filesz > ph->p_memsz) return ELF_ERR_TOO_SMALL;
    if (ph->p_filesz && ph->p_offset + ph->p_filesz > src->size) return ELF_ERR_TOO_SMALL;

    uint32_t prot = pf_to_vm(ph->p_flags);
    uintptr_t vstart = ph->p_vaddr + bias;

    uintptr_t pstart   = page_align_down(vstart);
    uintptr_t file_end = page_align_up(vstart + ph->p_filesz);
    uintptr_t mem_end  = page_align_up(vstart + ph->p_memsz);
    uint64_t  foff     = ph->p_offset - (vstart - pstart);

    if (ph->p_filesz) {
        if (mm_map_file(mm, pstart, file_end - pstart, prot, true, src->vn, foff) < 0)
            return ELF_ERR_NO_MEM;
        uintptr_t tail = vstart + ph->p_filesz;
        if ((tail & (PAGE_SIZE - 1)) && ph->p_memsz > ph->p_filesz) {
            if (!(prot & VM_WRITE)) mm_protect(mm, page_align_down(tail), PAGE_SIZE, prot | VM_WRITE);
            if (zero_user(mm, tail, page_align_up(tail) - tail) < 0) return ELF_ERR_NO_MEM;
            if (!(prot & VM_WRITE)) mm_protect(mm, page_align_down(tail), PAGE_SIZE, prot);
        }
    } else {
        file_end = pstart;
    }
    if (mem_end > file_end &&
        mm_map_anon(mm, file_end, mem_end - file_end, prot, true) < 0)
        return ELF_ERR_NO_MEM;
    return ELF_OK;
}

static bool needs_eager(const elf_source_t *src, const elf64_ehdr_t *eh, uintptr_t bias) {
    if (!src->vn) return true;
    uintptr_t prev_end = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(src, &ph, eh->e_phoff + (uint64_t)eh->e_phentsize * i, sizeof ph) < 0) return true;
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        uintptr_t v = ph.p_vaddr + bias;
        if ((ph.p_offset ^ v) & (PAGE_SIZE - 1)) return true;
        uintptr_t s = page_align_down(v);
        uintptr_t e = page_align_up(v + ph.p_memsz);
        if (s < prev_end) return true;
        prev_end = e;
    }
    return false;
}

static uintptr_t cover_orphan_sections(mm_t *mm, const elf64_ehdr_t *ehdr,
                                       const elf_source_t *src, uintptr_t load_bias,
                                       uintptr_t cur_load_end) {
    if (!ehdr->e_shnum || !ehdr->e_shoff) return cur_load_end;
    if (ehdr->e_shentsize < sizeof(elf64_shdr_t)) return cur_load_end;
    uint64_t sht_end = (uint64_t)ehdr->e_shoff + (uint64_t)ehdr->e_shentsize * ehdr->e_shnum;
    if (sht_end > src->size) return cur_load_end;

    uintptr_t new_end = cur_load_end;
    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        elf64_shdr_t sh;
        if (src_read(src, &sh, ehdr->e_shoff + (uint64_t)ehdr->e_shentsize * i, sizeof sh) < 0) break;
        if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_size == 0 || sh.sh_addr == 0) continue;

        uintptr_t s_start = (uintptr_t)sh.sh_addr + load_bias;
        uintptr_t s_end   = s_start + sh.sh_size;
        uintptr_t p_start = page_align_down(s_start);
        uintptr_t p_end   = page_align_up(s_end);
        uint32_t  prot    = VM_READ;
        if (sh.sh_flags & SHF_WRITE) prot |= VM_WRITE;
        if (sh.sh_flags & SHF_EXECINSTR) prot |= VM_EXEC;

        for (uintptr_t p = p_start; p < p_end; p += PAGE_SIZE) {
            if (mm_find_vma(mm, p)) continue;
            if (mm_map_anon(mm, p, PAGE_SIZE, prot | VM_WRITE, true) < 0) return new_end;
            uintptr_t cs = p > s_start ? p : s_start;
            uintptr_t ce = (p + PAGE_SIZE) < s_end ? (p + PAGE_SIZE) : s_end;
            if (sh.sh_type != SHT_NOBITS && ce > cs && sh.sh_offset + sh.sh_size <= src->size)
                copy_from_source(mm, src, cs, sh.sh_offset + (cs - s_start), ce - cs);
            if (!(prot & VM_WRITE)) mm_protect(mm, p, PAGE_SIZE, prot);
        }
        if (p_end > new_end) new_end = p_end;
    }
    return new_end;
}

static elf_error_t load_interp(mm_t *mm, const char *path, elf_load_result_t *result) {
    vfs_file_t *f = NULL;
    if (vfs_open(path, 0, 0, &f) < 0 || !f) return ELF_ERR_NULL;

    vfs_stat_t st;
    if (vfs_fstat(f, &st) < 0) { vfs_close(f); return ELF_ERR_NULL; }

    elf_source_t isrc = { .buf = NULL, .vn = f->vnode, .size = (size_t)st.st_size };

    elf64_ehdr_t ehdr;
    if (src_read(&isrc, &ehdr, 0, sizeof ehdr) < 0) { vfs_close(f); return ELF_ERR_TOO_SMALL; }

    elf_error_t verr = elf_validate(&ehdr, isrc.size);
    if (verr != ELF_OK) { vfs_close(f); return verr; }

    uintptr_t bias = (ehdr.e_type == ET_DYN) ? ELF_INTERP_BASE : 0;
    bool eager = needs_eager(&isrc, &ehdr, bias);
    bool any = false;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(&isrc, &ph, ehdr.e_phoff + (uint64_t)ehdr.e_phentsize * i,
                     sizeof ph) < 0) { vfs_close(f); return ELF_ERR_TOO_SMALL; }
        if (ph.p_type != PT_LOAD) continue;
        any = true;
        if (eager) continue;
        elf_error_t err = load_segment(mm, &isrc, &ph, bias);
        if (err != ELF_OK) { vfs_close(f); return err; }
    }
    if (eager && any) {
        elf_error_t err = load_all_eager(mm, &isrc, &ehdr, bias);
        if (err != ELF_OK) { vfs_close(f); return err; }
    }
    vfs_close(f);

    if (!any) return ELF_ERR_NO_LOAD;

    result->has_interp   = 1;
    result->interp_base  = bias;
    result->interp_entry = ehdr.e_entry + bias;
    return ELF_OK;
}

static elf_load_result_t elf_load_core(const elf_source_t* src, size_t stack_sz) {
    elf_load_result_t result;
    memset(&result, 0, sizeof result);

    elf64_ehdr_t ehdr_buf;
    if (src_read(src, &ehdr_buf, 0, sizeof(ehdr_buf)) < 0) {
        result.error = ELF_ERR_TOO_SMALL;
        return result;
    }
    const elf64_ehdr_t* ehdr = &ehdr_buf;

    result.error = elf_validate(ehdr, src->size);
    if (result.error != ELF_OK) {
        serial_printf("[ELF] Validation failed: %s\n", elf_strerror(result.error));
        return result;
    }

    uintptr_t load_bias = (ehdr->e_type == ET_DYN) ? ELF_PIE_BASE : 0;

    mm_t *mm = mm_create();
    if (!mm) {
        result.error = ELF_ERR_NO_MEM;
        return result;
    }
    result.mm = mm;
    result.pagemap = mm->pagemap;

    bool eager = needs_eager(src, ehdr, load_bias);
    bool has_load = false;
    uintptr_t max_vaddr = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(src, &ph, ehdr->e_phoff + (uint64_t)ehdr->e_phentsize * i,
                     sizeof(ph)) < 0) {
            result.error = ELF_ERR_TOO_SMALL;
            return result;
        }
        if (ph.p_type != PT_LOAD) continue;
        has_load = true;

        if (!eager) {
            elf_error_t err = load_segment(mm, src, &ph, load_bias);
            if (err != ELF_OK) {
                result.error = err;
                return result;
            }
        }

        if (result.nsegments < ELF_MAX_SEGMENTS) {
            elf_segment_t* s = &result.segments[result.nsegments++];
            s->vaddr  = ph.p_vaddr + load_bias;
            s->offset = ph.p_offset;
            s->filesz = ph.p_filesz;
            s->memsz  = ph.p_memsz;
            s->flags  = ph.p_flags;
        }

        uintptr_t seg_end = ph.p_vaddr + load_bias + ph.p_memsz;
        if (seg_end > max_vaddr) max_vaddr = seg_end;
    }

    if (!has_load) { result.error = ELF_ERR_NO_LOAD; return result; }
    if (eager) {
        elf_error_t err = load_all_eager(mm, src, ehdr, load_bias);
        if (err != ELF_OK) { result.error = err; return result; }
    }

    result.phnum     = ehdr->e_phnum;
    result.phentsize = ehdr->e_phentsize;
    result.phdr_vaddr = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        elf64_phdr_t ph;
        if (src_read(src, &ph, ehdr->e_phoff + (uint64_t)ehdr->e_phentsize * i,
                     sizeof(ph)) < 0) break;
        if (ph.p_type == PT_PHDR && !result.phdr_vaddr)
            result.phdr_vaddr = ph.p_vaddr + load_bias;
        if (ph.p_type == PT_TLS) {
            result.tls_vaddr  = ph.p_vaddr + load_bias;
            result.tls_filesz = ph.p_filesz;
            result.tls_memsz  = ph.p_memsz;
            result.tls_align  = ph.p_align ? ph.p_align : 1;
        }
        if (ph.p_type == PT_INTERP && !result.interp_path[0]) {
            size_t n = ph.p_filesz;
            if (n && n < sizeof result.interp_path &&
                src_read(src, result.interp_path, ph.p_offset, n) == 0)
                result.interp_path[n] = 0;
            else
                result.interp_path[0] = 0;
        }
    }
    if (!result.phdr_vaddr) {
        for (int i = 0; i < result.nsegments; i++) {
            const elf_segment_t *sg = &result.segments[i];
            if (ehdr->e_phoff >= sg->offset &&
                ehdr->e_phoff < sg->offset + sg->filesz) {
                result.phdr_vaddr = sg->vaddr + (ehdr->e_phoff - sg->offset);
                break;
            }
        }
    }

    uintptr_t load_end = page_align_up(max_vaddr);
    load_end = cover_orphan_sections(mm, ehdr, src, load_bias, load_end);

    result.entry     = ehdr->e_entry + load_bias;
    result.load_base = load_bias;
    result.load_end  = load_end;

    if (result.interp_path[0]) {
        elf_error_t ierr = load_interp(mm, result.interp_path, &result);
        if (ierr != ELF_OK) {
            serial_printf("[ELF] cannot load interpreter %s: %s\n",
                          result.interp_path, elf_strerror(ierr));
            result.error = ierr;
            return result;
        }
    }

    if (mm_setup_stack(mm) < 0) { result.error = ELF_ERR_NO_MEM; return result; }
    if (stack_sz == 0) stack_sz = ELF_DEFAULT_STACK;
    result.stack_size = stack_sz;
    result.stack_top  = (MM_STACK_TOP - 8) & ~(uintptr_t)0xF;
    return result;
}

elf_load_result_t elf_load(const void* data, size_t size, size_t stack_sz) {
    elf_load_result_t result;
    memset(&result, 0, sizeof result);
    if (!data) { result.error = ELF_ERR_NULL; return result; }
    elf_source_t src = { .buf = (const uint8_t*)data, .vn = NULL, .size = size };
    return elf_load_core(&src, stack_sz);
}

elf_load_result_t elf_load_vnode(vnode_t* vn, size_t size, size_t stack_sz) {
    elf_load_result_t result;
    memset(&result, 0, sizeof result);
    if (!vn) { result.error = ELF_ERR_NULL; return result; }
    elf_source_t src = { .buf = NULL, .vn = vn, .size = size };
    result = elf_load_core(&src, stack_sz);
    result.vnode = vn;
    result.file_size = size;
    return result;
}

void elf_unload(elf_load_result_t* result) {
    if (!result) return;
    if (result->mm) {
        mm_put(result->mm);
        result->mm = NULL;
        result->pagemap = NULL;
    } else if (result->pagemap) {
        vmm_free_pagemap(result->pagemap);
        result->pagemap = NULL;
    }
}

const char* elf_strerror(elf_error_t err) {
    switch (err) {
        case ELF_OK:              return "OK";
        case ELF_ERR_NULL:        return "NULL pointer";
        case ELF_ERR_TOO_SMALL:   return "file too small / data out of bounds";
        case ELF_ERR_BAD_MAGIC:   return "not an ELF file (bad magic)";
        case ELF_ERR_NOT_64:      return "not ELF64";
        case ELF_ERR_NOT_LE:      return "not little-endian";
        case ELF_ERR_BAD_VERSION: return "unsupported ELF version";
        case ELF_ERR_NOT_EXEC:    return "not an executable or shared object";
        case ELF_ERR_WRONG_ARCH:  return "not x86_64";
        case ELF_ERR_NO_LOAD:     return "no PT_LOAD segments";
        case ELF_ERR_MAP_FAIL:    return "vmm_map_page failed";
        case ELF_ERR_NO_MEM:      return "out of memory";
        default:                  return "unknown error";
    }
}
