#include "../../include/puzzle/puzzle.h"
#include "../../include/sched/sched.h"
#include "../../include/io/serial.h"
#include "../../include/memory/pmm.h"
#include "../../include/memory/vmm.h"
#include "../../include/smp/percpu.h"
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
    pp->alive_pieces++;
    return p;
}

puzzle_process_t *puzzle_create(struct task *t,
                                const elf_load_result_t *elf,
                                uintptr_t heap_start,
                                uintptr_t stack_top, size_t stack_size,
                                void *elf_file, size_t elf_file_size) {
    if (!t || !elf) return NULL;
    puzzle_process_t *pp = calloc(1, sizeof(*pp));
    if (!pp) return NULL;

    pp->logic         = t;
    pp->pagemap       = elf->pagemap;
    pp->elf_file      = elf_file;
    pp->elf_file_size = elf_file_size;
    pp->ckpt_seq      = 0;

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
    if (p->kind == PIECE_HEAP && pp->logic) {
        *start = pp->logic->brk_start;
        *end   = pp->logic->brk_current;
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

    *pte |= VMM_WRITE;
    vmm_flush_local(page);
    spinlock_release_irqrestore(&pp->lock, fl);
    return true;
}

bool puzzle_regenerate_fault(struct task *t, uintptr_t fault_addr, uint64_t err_code) {
    if (!g_puzzle_regen_enabled) return false;
    if (err_code & 0x1) return false;
    if (!t || !t->puzzle) return false;
    puzzle_process_t *pp = t->puzzle;

    uintptr_t page = fault_addr & ~(PAGE_SIZE - 1);
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);

    piece_t *hit = NULL;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (page >= start && page < end) { hit = p; break; }
    }
    if (!hit) { spinlock_release_irqrestore(&pp->lock, fl); return false; }

    vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, page);
    if (pte && (*pte & VMM_PRESENT)) {
        spinlock_release_irqrestore(&pp->lock, fl);
        return false;
    }

    pp->total_eaten++;
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

    void *frame = pmm_alloc_zero(1);
    if (!frame) { spinlock_release_irqrestore(&pp->lock, fl); return false; }

    if (pp->elf_file && hit->file_len > 0) {
        uintptr_t fstart = hit->va_start;
        uintptr_t fend   = hit->va_start + hit->file_len;
        uintptr_t cs = page > fstart ? page : fstart;
        uintptr_t ce = (page + PAGE_SIZE) < fend ? (page + PAGE_SIZE) : fend;
        if (ce > cs) {
            uint64_t foff = hit->elf_off + (cs - hit->va_start);
            if (foff + (ce - cs) <= pp->elf_file_size)
                memcpy((uint8_t *)frame + (cs - page),
                       (uint8_t *)pp->elf_file + foff, (size_t)(ce - cs));
        }
    }

    uint64_t flags = VMM_PRESENT | VMM_USER;
    if (hit->prot & PF_W) flags |= VMM_WRITE;
    vmm_map_page(pp->pagemap, page, pmm_virt_to_phys(frame), flags);
    vmm_flush_local(page);

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

    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);
    int unmapped = 0;
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->kind != (piece_kind_t)kind || p->state == PIECE_EATEN) continue;
        uintptr_t start, end;
        piece_range(pp, p, &start, &end);
        start &= ~(PAGE_SIZE - 1);
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
            vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, va);
            if (!pte || !(*pte & VMM_PRESENT)) continue;
            vmm_unmap_page(pp->pagemap, va);
            unmapped++;
        }
    }
    spinlock_release_irqrestore(&pp->lock, fl);

    serial_printf("[puzzle] pid=%u '%s' ATE %s: unmapped %d page(s)\n",
                  pp->logic ? pp->logic->pid : 0,
                  pp->logic ? pp->logic->name : "?",
                  piece_kind_name((piece_kind_t)kind), unmapped);
    return unmapped;
}

static bool piece_golden_page(puzzle_process_t *pp, piece_t *p, uintptr_t page,
                              const uint8_t **golden, uint32_t *off_in_page,
                              uint32_t *len) {
    if (!pp->elf_file || p->file_len == 0) return false;
    uintptr_t fstart = p->va_start;
    uintptr_t fend   = p->va_start + p->file_len;
    uintptr_t cs = page > fstart ? page : fstart;
    uintptr_t ce = (page + PAGE_SIZE) < fend ? (page + PAGE_SIZE) : fend;
    if (ce <= cs) return false;
    uint64_t foff = p->elf_off + (cs - p->va_start);
    if (foff + (ce - cs) > pp->elf_file_size) return false;
    *golden      = (const uint8_t *)pp->elf_file + foff;
    *off_in_page = (uint32_t)(cs - page);
    *len         = (uint32_t)(ce - cs);
    return true;
}

static int puzzle_guardian_scan(puzzle_process_t *pp) {
    int restored = 0;
    uint64_t fl = spinlock_acquire_irqsave(&pp->lock);
    for (int i = 0; i < pp->npieces; i++) {
        piece_t *p = &pp->pieces[i];
        if (p->state == PIECE_EATEN) continue;
        if (p->prot & PF_W) continue;
        if (p->file_len == 0 || !pp->elf_file) continue;

        uintptr_t start = p->va_start & ~(PAGE_SIZE - 1);
        uintptr_t end   = (p->va_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
            vmm_pte_t *pte = vmm_pte_lookup(pp->pagemap, va);
            if (!pte || !(*pte & VMM_PRESENT)) continue;
            const uint8_t *golden; uint32_t off, len;
            if (!piece_golden_page(pp, p, va, &golden, &off, &len)) continue;
            uint8_t *cur = (uint8_t *)pmm_phys_to_virt(*pte & PUZZLE_PTE_PHYS) + off;
            if (memcmp(cur, golden, len) != 0) {
                memcpy(cur, golden, len);
                pp->total_guard_restores++;
                restored++;
                serial_printf("[guardian] pid=%u '%s' TAMPER in %s page 0x%llx -> restored from source of truth (total %llu)\n",
                              pp->logic ? pp->logic->pid : 0,
                              pp->logic ? pp->logic->name : "?",
                              piece_kind_name(p->kind), (unsigned long long)va,
                              (unsigned long long)pp->total_guard_restores);
            }
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

    uint32_t off = (uint32_t)(addr - page);
    if (off + len > PAGE_SIZE) len = PAGE_SIZE - off;
    uint8_t *cur = (uint8_t *)pmm_phys_to_virt(*pte & PUZZLE_PTE_PHYS) + off;
    memset(cur, 0xEE, len);
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
    if (pp->elf_file) { free(pp->elf_file); pp->elf_file = NULL; }
    if (pp->logic) pp->logic->puzzle = NULL;
    free(pp);
}

void puzzle_dump(const puzzle_process_t *pp) {
    if (!pp) return;
    serial_printf("[puzzle] pid=%u '%s': %d pieces (alive=%d)\n",
                  pp->logic ? pp->logic->pid : 0,
                  pp->logic ? pp->logic->name : "?",
                  pp->npieces, pp->alive_pieces);
    for (int i = 0; i < pp->npieces; i++) {
        const piece_t *p = &pp->pieces[i];
        char r = (p->prot & PF_R) ? 'r' : '-';
        char w = (p->prot & PF_W) ? 'w' : '-';
        char x = (p->prot & PF_X) ? 'x' : '-';
        if (p->lives == PIECE_LIVES_INF)
            serial_printf("[puzzle]   #%d %-6s [0x%llx..0x%llx] %c%c%c lives=INF\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x);
        else
            serial_printf("[puzzle]   #%d %-6s [0x%llx..0x%llx] %c%c%c lives=%d\n",
                          i, piece_kind_name(p->kind),
                          (unsigned long long)p->va_start,
                          (unsigned long long)p->va_end, r, w, x, p->lives);
    }
    serial_printf("[puzzle]   ckpt seq=%llu armed=%d undo=%u snapshots=%llu%s\n",
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
