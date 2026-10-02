#include "../../include/puzzle/puzzle.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/smp/percpu.h"
#include "../../include/memory/mm.h"
#include "../../include/memory/pageref.h"
#include "../../include/memory/pagecache.h"
#include "../../include/fs/vfs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern uint64_t sched_now_ns(void);

static bool g_puzzle_ckpt_enabled = false;
static bool g_puzzle_regen_enabled = true;

void puzzle_set_checkpointing(bool on) { g_puzzle_ckpt_enabled = on; }
void puzzle_set_regen(bool on) { g_puzzle_regen_enabled = on; }

#define PUZZLE_MAX_PROCS 64
static puzzle_process_t *g_procs[PUZZLE_MAX_PROCS];
static int               g_proc_count = 0;
static spinlock_t        g_proc_lock = SPINLOCK_INIT;
static bool              g_guardian_started = false;

static void puzzle_register(puzzle_process_t *pp) {
    uint64_t f = spinlock_acquire_irqsave(&g_proc_lock);
    if (g_proc_count < PUZZLE_MAX_PROCS) g_procs[g_proc_count++] = pp;
    spinlock_release_irqrestore(&g_proc_lock, f);
}

static void puzzle_unregister(puzzle_process_t *pp) {
    uint64_t f = spinlock_acquire_irqsave(&g_proc_lock);
    for (int i = 0; i < g_proc_count; i++) {
        if (g_procs[i] == pp) { g_procs[i] = g_procs[--g_proc_count]; break; }
    }
    spinlock_release_irqrestore(&g_proc_lock, f);
}

#define LIVES_DATA   3
#define LIVES_STACK  2

#define PUZZLE_CKPT_INTERVAL_NS  1000000000ULL
#define PUZZLE_PTE_PHYS          0x000FFFFFFFFFF000ULL

static const char *kind_names[PIECE_KIND_MAX] = {
    "TEXT", "RODATA", "DATA", "BSS", "HEAP", "STACK"
};

const char *piece_kind_name(piece_kind_t k) {
    if ((int)k < 0 || k >= PIECE_KIND_MAX) return "?";
    return kind_names[k];
}

static piece_t *add_piece(puzzle_process_t *pp, piece_kind_t kind,
                          uintptr_t start, uintptr_t end,
                          uint32_t prot, int32_t lives,
                          uint64_t elf_off, uint64_t file_len) {
    if (pp->npieces >= PUZZLE_MAX_PIECES) return NULL;
    piece_t *p = &pp->pieces[pp->npieces++];
    p->kind     = kind;
    p->va_start = start;
    p->va_end   = end;
    p->prot     = prot;
    p->state    = PIECE_ALIVE;
    p->lives    = lives;
    p->elf_off  = elf_off;
    p->file_len = file_len;
    p->eaten_map = NULL;
    p->eaten_pages = 0;
    pp->alive_pieces++;
    return p;
}

puzzle_process_t *puzzle_create(struct task *t,
                                const elf_load_result_t *elf,
                                uintptr_t heap_start,
                                uintptr_t stack_top, size_t stack_size,
                                struct vnode *vn, uint64_t file_size) {
    if (!t || !elf || !elf->mm) return NULL;
    puzzle_process_t *pp = calloc(1, sizeof(*pp));
    if (!pp) return NULL;

    pp->logic         = t;
    pp->mm            = elf->mm;
    pp->pagemap       = elf->mm->pagemap;
    pp->vnode         = vn;
    pp->file_size     = file_size;
    pp->ckpt_seq      = 0;
    if (vn) vnode_ref(vn);

    for (int i = 0; i < elf->nsegments; i++) {
        const elf_segment_t *s = &elf->segments[i];
        uint32_t prot = s->flags & (PF_R | PF_W | PF_X);
        if (s->flags & PF_X) {
            add_piece(pp, PIECE_TEXT, s->vaddr, s->vaddr + s->memsz,
                      prot, PIECE_LIVES_INF, s->offset, s->filesz);
        } else if (!(s->flags & PF_W)) {
            add_piece(pp, PIECE_RODATA, s->vaddr, s->vaddr + s->memsz,
                      prot, PIECE_LIVES_INF, s->offset, s->filesz);
        } else {
            uintptr_t data_end = s->vaddr + s->filesz;
            uintptr_t bss_end  = s->vaddr + s->memsz;
            if (s->filesz > 0)
                add_piece(pp, PIECE_DATA, s->vaddr, data_end, prot,
                          LIVES_DATA, s->offset, s->filesz);
            if (bss_end > data_end)
                add_piece(pp, PIECE_BSS, data_end, bss_end, prot,
                          LIVES_DATA, 0, 0);
        }
    }

    add_piece(pp, PIECE_HEAP, heap_start, heap_start,
              PF_R | PF_W, LIVES_DATA, 0, 0);
    add_piece(pp, PIECE_STACK, stack_top - stack_size, stack_top,
              PF_R | PF_W, LIVES_STACK, 0, 0);

    t->puzzle = pp;
    puzzle_register(pp);
    puzzle_dump(pp);
    return pp;
}

static bool piece_mutable(piece_kind_t k) {
    return k == PIECE_DATA || k == PIECE_BSS ||
           k == PIECE_HEAP || k == PIECE_STACK;
}

static void piece_range(const puzzle_process_t *pp, const piece_t *p,
                        uintptr_t *start, uintptr_t *end) {
    if (p->kind == PIECE_HEAP && pp->mm) {
        *start = pp->mm->brk_start;
        *end   = pp->mm->brk;
    } else {
        *start = p->va_start;
        *end   = p->va_end;
    }
}

static void free_undo_log(puzzle_process_t *pp) {
    puzzle_undo_t *u = pp->undo;
    while (u) {
        puzzle_undo_t *nx = u->next;
        if (u->shadow_phys)
            pmm_free(pmm_phys_to_virt(u->shadow_phys), 1);
        kfree(u);
        u = nx;
    }
    pp->undo = NULL;
    pp->undo_count = 0;
}

void puzzle_checkpoint_arm(puzzle_process_t *pp) {
    if (!pp || !pp->pagemap) return;
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    uint32_t prev_snaps = pp->undo_count;
    free_undo_log(pp);
    pp->degraded = false;

    uint64_t nprot = 0;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->state == PIECE_EATEN || !piece_mutable(p->kind)) continue;

        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
            vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, va);
            if (!pte) continue;
            vmm_pte_t e = *pte;
            if (!(e & VMM_PRESENT) || !(e & VMM_WRITE)) continue;
            *pte = e & ~VMM_WRITE;
            vmm_flush_local(va);
            nprot++;
        }
    }

    pp->armed = nprot > 0;
    pp->ckpt_seq++;
    pp->total_checkpoints++;
    pp->last_arm_ns = sched_now_ns();

    if (nprot > 0 || prev_snaps > 0)
        serial_printf("[puzzle] pid=%u '%s' ckpt#%llu: protected %llu pages, %u dirtied last interval\n",
                      pp->logic ? pp->logic->pid : 0,
                      pp->logic ? pp->logic->name : "?",
                      (unsigned long long)pp->ckpt_seq,
                      (unsigned long long)nprot, prev_snaps);
    spinlock_release_irqrestore(&pp->lock, fl);
}

void puzzle_on_syscall(struct task *t) {
    if (!g_puzzle_ckpt_enabled) return;
    if (!t || !t->puzzle) return;
    puzzle_process_t *pp = t->puzzle;
    uint64_t now = sched_now_ns();
    if (pp->last_arm_ns != 0 && now - pp->last_arm_ns < PUZZLE_CKPT_INTERVAL_NS)
        return;
    puzzle_checkpoint_arm(pp);
}

bool puzzle_cow_fault(struct task *t, uintptr_t fault_addr, uint64_t err_code) {
    if (!g_puzzle_ckpt_enabled) return false;
    if ((err_code & 0x3) != 0x3) return false;
    if (!t || !t->puzzle) return false;
    puzzle_process_t *pp = t->puzzle;

    uintptr_t page = fault_addr & ~(PAGE_SIZE - 1);
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    piece_t *hit = NULL;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->state == PIECE_EATEN || !piece_mutable(p->kind)) continue;
        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (page >= start && page < end) { hit = p; break; }
    }
    if (!hit) { spinlock_release_irqrestore(&pp->lock, fl); return false; }

    vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, page);
    if (!pte || !(*pte & VMM_PRESENT)) {
        spinlock_release_irqrestore(&pp->lock, fl);
        return false;
    }
    if (*pte & VMM_WRITE) {
        spinlock_release_irqrestore(&pp->lock, fl);
        return true;
    }

    void *shadow = pmm_alloc(1);
    if (shadow) {
        uintptr_t cur_phys = *pte & PUZZLE_PTE_PHYS;
        memcpy(shadow, pmm_phys_to_virt(cur_phys), PAGE_SIZE);
        puzzle_undo_t *u = kmalloc(sizeof(*u));
        if (u) {
            u->page_va     = page;
            u->shadow_phys = pmm_virt_to_phys(shadow);
            u->next        = pp->undo;
            pp->undo       = u;
            pp->undo_count++;
            pp->total_snapshots++;
        } else {
            pmm_free(shadow, 1);
            pp->degraded = true;
        }
    } else {
        pp->degraded = true;
    }

    spinlock_release_irqrestore(&pp->lock, fl);
    return true;
}

static bool eaten_test_clear(piece_t *p, uintptr_t start, uintptr_t page) {
    if (!p->eaten_map || page < start) return false;
    size_t idx = (size_t)((page - start) >> 12);
    if (idx >= p->eaten_pages) return false;
    uint8_t bit = (uint8_t)(1u << (idx & 7));
    if (!(p->eaten_map[idx >> 3] & bit)) return false;
    p->eaten_map[idx >> 3] &= (uint8_t)~bit;
    return true;
}

bool puzzle_regenerate_fault(struct task *t, uintptr_t fault_addr, uint64_t err_code) {
    if (!g_puzzle_regen_enabled) return true;
    if (err_code & 0x1) return true;
    if (!t || !t->puzzle) return true;
    puzzle_process_t *pp = t->puzzle;
    if (__atomic_load_n(&pp->total_eaten, __ATOMIC_RELAXED) ==
        __atomic_load_n(&pp->total_regens, __ATOMIC_RELAXED)) return true;

    uintptr_t page = fault_addr & ~(PAGE_SIZE - 1);
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    piece_t *hit = NULL;
    uintptr_t hit_start = 0;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (page >= start && page < end) { hit = p; hit_start = start; break; }
    }
    if (!hit || !eaten_test_clear(hit, hit_start, page)) {
        spinlock_release_irqrestore(&pp->lock, fl);
        return true;
    }

    if (hit->lives != PIECE_LIVES_INF) {
        if (hit->lives <= 0) {
            if (hit->state != PIECE_EATEN) {
                hit->state = PIECE_EATEN;
                if (pp->alive_pieces > 0) pp->alive_pieces--;
            }
            serial_printf("[puzzle] pid=%u '%s' piece %s fully EATEN (out of lives) -> fatal\n",
                          pp->logic ? pp->logic->pid : 0,
                          pp->logic ? pp->logic->name : "?",
                          piece_kind_name(hit->kind));
            spinlock_release_irqrestore(&pp->lock, fl);
            return false;
        }
        hit->lives--;
    }

    pp->total_regens++;
    serial_printf("[puzzle] pid=%u '%s' regen %s page 0x%llx (lives=%d, total regens=%llu)\n",
                  pp->logic ? pp->logic->pid : 0,
                  pp->logic ? pp->logic->name : "?",
                  piece_kind_name(hit->kind), (unsigned long long)page,
                  hit->lives, (unsigned long long)pp->total_regens);
    spinlock_release_irqrestore(&pp->lock, fl);
    return true;
}

int puzzle_eat_piece(struct task *t, int kind) {
    if (!t || !t->puzzle) return -1;
    if (kind < 0 || kind >= PIECE_KIND_MAX) return -1;
    puzzle_process_t *pp = t->puzzle;

    typedef struct { uintptr_t start, end; } rng_t;
    rng_t ranges[PUZZLE_MAX_PIECES];
    int nr = 0;
    int marked = 0;

    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->kind != (piece_kind_t)kind || p->state == PIECE_EATEN) continue;
        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (end <= start) continue;
        size_t npages = (size_t)((end - start) >> 12);
        if (p->eaten_pages != npages) {
            kfree(p->eaten_map);
            p->eaten_map = kzalloc((npages + 7) / 8);
            p->eaten_pages = p->eaten_map ? npages : 0;
        }
        if (!p->eaten_map) continue;
        for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
            vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, va);
            if (!pte || !(*pte & VMM_PRESENT)) continue;
            size_t idx = (size_t)((va - start) >> 12);
            p->eaten_map[idx >> 3] |= (uint8_t)(1u << (idx & 7));
            marked++;
        }
        ranges[nr].start = start;
        ranges[nr].end = end;
        nr++;
    }
    spinlock_release_irqrestore(&pp->lock, fl);

    for (int i = 0; i < nr; i++)
        mm_discard(pp->mm, ranges[i].start, ranges[i].end - ranges[i].start);
    pp->total_eaten += (uint64_t)marked;

    serial_printf("[puzzle] pid=%u '%s' ATE %s: unmapped %d page(s)\n",
                  pp->logic ? pp->logic->pid : 0,
                  pp->logic ? pp->logic->name : "?",
                  piece_kind_name((piece_kind_t)kind), marked);
    return marked;
}

static int puzzle_guardian_scan(puzzle_process_t *pp) {
    int restored = 0;
    if (!pp->vnode) return 0;
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->state == PIECE_EATEN) continue;
        if (p->prot & PF_W) continue;
        if (p->file_len == 0) continue;

        uintptr_t start = p->va_start & ~(PAGE_SIZE - 1);
        uintptr_t end   = (p->va_start + p->file_len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t  fpage = (p->elf_off - (p->va_start - start)) / PAGE_SIZE;
        for (uintptr_t va = start; va < end; va += PAGE_SIZE, fpage++) {
            vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, va);
            if (!pte || !(*pte & VMM_PRESENT)) continue;
            uintptr_t cur = *pte & PUZZLE_PTE_PHYS;
            uintptr_t gold = 0;
            if (pagecache_peek(pp->vnode, fpage, &gold) < 0) continue;
            if (gold != cur) {
                uintptr_t cs = va > p->va_start ? va : p->va_start;
                uintptr_t ce = (va + PAGE_SIZE) < p->va_start + p->file_len
                             ? (va + PAGE_SIZE) : p->va_start + p->file_len;
                uint8_t *dst = (uint8_t *)pmm_phys_to_virt(cur) + (cs - va);
                const uint8_t *src = (const uint8_t *)pmm_phys_to_virt(gold) + (cs - va);
                if (ce > cs && memcmp(dst, src, (size_t)(ce - cs)) != 0) {
                    memcpy(dst, src, (size_t)(ce - cs));
                    pp->total_guard_restores++;
                    restored++;
                    serial_printf("[guardian] pid=%u '%s' TAMPER in %s page 0x%llx -> restored from source of truth (total %llu)\n",
                                  pp->logic ? pp->logic->pid : 0,
                                  pp->logic ? pp->logic->name : "?",
                                  piece_kind_name(p->kind), (unsigned long long)va,
                                  (unsigned long long)pp->total_guard_restores);
                }
            }
            page_unref(gold);
        }
    }
    spinlock_release_irqrestore(&pp->lock, fl);
    return restored;
}

#define PUZZLE_GUARD_INTERVAL_MS 50

static void puzzle_guardian_task(void *arg) {
    (void)arg;
    serial_printf("[guardian] integrity guardian running on cpu %u\n", smp_cpu_index());
    int idx = 0;
    while (1) {
        uint64_t f = spinlock_acquire_irqsave(&g_proc_lock);
        if (g_proc_count > 0) {
            if (idx >= g_proc_count) idx = 0;
            puzzle_process_t *pp = g_procs[idx++];
            if (pp) puzzle_guardian_scan(pp);
        }
        spinlock_release_irqrestore(&g_proc_lock, f);
        task_sleep_ms(PUZZLE_GUARD_INTERVAL_MS);
    }
}

void puzzle_guardian_start(void) {
    if (g_guardian_started) return;
    g_guardian_started = true;
    task_create_ex("puzzle-guardian", puzzle_guardian_task, NULL, 0, 0);
}

int puzzle_tamper(struct task *t, uintptr_t addr, size_t len) {
    if (!t || !t->puzzle || len == 0) return -1;
    puzzle_process_t *pp = t->puzzle;
    uintptr_t page = addr & ~(PAGE_SIZE - 1);
    if (mm_populate(pp->mm, page, PAGE_SIZE, 0) < 0) return -1;
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    piece_t *hit = NULL;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if ((p->prot & PF_W) || p->file_len == 0) continue;
        if (addr >= p->va_start && addr < p->va_end) { hit = p; break; }
    }
    if (!hit) { spinlock_release_irqrestore(&pp->lock, fl); return -1; }

    vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, page);
    if (!pte || !(*pte & VMM_PRESENT)) { spinlock_release_irqrestore(&pp->lock, fl); return -1; }

    uintptr_t cur = *pte & PUZZLE_PTE_PHYS;
    if (!(*pte & VMM_SHARED) && page_refcount(cur) > 1) {
        uintptr_t np = page_alloc_user();
        if (!np) { spinlock_release_irqrestore(&pp->lock, fl); return -1; }
        memcpy(pmm_phys_to_virt(np), pmm_phys_to_virt(cur), PAGE_SIZE);
        *pte = (*pte & ~PUZZLE_PTE_PHYS) | np;
        vmm_flush_local(page);
        page_unref(cur);
        cur = np;
    }

    uint32_t off = (uint32_t)(addr - page);
    if (off + len > PAGE_SIZE) len = PAGE_SIZE - off;
    uint8_t *mem = (uint8_t *)pmm_phys_to_virt(cur) + off;
    memset(mem, 0xEE, len);
    serial_printf("[puzzle] pid=%u '%s' SELF-TAMPER: %zu bytes 0xEE at 0x%llx (%s) -- guardian should heal\n",
                  pp->logic ? pp->logic->pid : 0, pp->logic ? pp->logic->name : "?",
                  len, (unsigned long long)addr, piece_kind_name(hit->kind));
    spinlock_release_irqrestore(&pp->lock, fl);
    return (int)len;
}

void puzzle_destroy(puzzle_process_t *pp) {
    if (!pp) return;
    puzzle_unregister(pp);
    free_undo_log(pp);
    for (int i = 0; i < pp->npieces; i++) {
        kfree(pp->pieces[i].eaten_map);
        pp->pieces[i].eaten_map = NULL;
    }
    if (pp->vnode) { vnode_unref(pp->vnode); pp->vnode = NULL; }
    if (pp->logic) pp->logic->puzzle = NULL;
    free(pp);
}

void puzzle_dump(const puzzle_process_t *pp) {
    if (!pp) return;
    LOG_D("[puzzle] pid=%u '%s': %d pieces (alive=%d)\n",
                  pp->logic ? pp->logic->pid : 0,
                  pp->logic ? pp->logic->name : "?",
                  pp->npieces, pp->alive_pieces);
    for (int i = 0; i < pp->npieces; i++) {
        const piece_t *p = &pp->pieces[i];
        char r = (p->prot & PF_R) ? 'r' : '-';
        char w = (p->prot & PF_W) ? 'w' : '-';
        char x = (p->prot & PF_X) ? 'x' : '-';
        if (p->lives == PIECE_LIVES_INF)
            LOG_D("[puzzle]   #%d %-6s [0x%llx..0x%llx] %c%c%c lives=INF\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x);
        else
            LOG_D("[puzzle]   #%d %-6s [0x%llx..0x%llx] %c%c%c lives=%d\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x, p->lives);
    }
    LOG_D("[puzzle]   ckpt seq=%llu armed=%d undo=%u snapshots=%llu%s\n",
                  (unsigned long long)pp->ckpt_seq, pp->armed ? 1 : 0,
                  pp->undo_count, (unsigned long long)pp->total_snapshots,
                  pp->degraded ? " DEGRADED" : "");
}

int puzzle_format(puzzle_process_t *pp, char *buf, size_t max) {
    if (!pp || !buf || max == 0) return 0;
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    int n = snprintf(buf, max, "process '%s' pid=%u: %d pieces (alive=%d)\n",
                     pp->logic ? pp->logic->name : "?",
                     pp->logic ? pp->logic->pid : 0,
                     pp->npieces, pp->alive_pieces);

    for (int i = 0; i < pp->npieces && n > 0 && (size_t)n < max; i++) {
        piece_t *p = &pp->pieces[i];
        char r = (p->prot & PF_R) ? 'r' : '-';
        char w = (p->prot & PF_W) ? 'w' : '-';
        char x = (p->prot & PF_X) ? 'x' : '-';
        const char *eaten = (p->state == PIECE_EATEN) ? " EATEN" : "";
        if (p->lives == PIECE_LIVES_INF)
            n += snprintf(buf + n, max - n,
                          "  #%d %-6s [0x%llx..0x%llx] %c%c%c lives=INF%s\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x, eaten);
        else
            n += snprintf(buf + n, max - n,
                          "  #%d %-6s [0x%llx..0x%llx] %c%c%c lives=%d%s\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x, p->lives, eaten);
    }
    if (n > 0 && (size_t)n < max)
        n += snprintf(buf + n, max - n,
                      "regens=%llu eaten=%llu guard_restores=%llu checkpoints=%llu\n",
                      (unsigned long long)pp->total_regens,
                      (unsigned long long)pp->total_eaten,
                      (unsigned long long)pp->total_guard_restores,
                      (unsigned long long)pp->total_checkpoints);

    spinlock_release_irqrestore(&pp->lock, fl);
    return n;
}
