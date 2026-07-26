#include "../../include/sched/spec.h"
#include "../../include/sched/sched.h"
#include "../../include/smp/smp.h"
#include "../../include/smp/percpu.h"
#include "../../include/apic/apic.h"
#include "../../include/memory/pmm.h"
#include "../../include/io/serial.h"
#include <stdlib.h>
#include <string.h>

#define SPEC_PAGE       4096UL
#define SPEC_MAX_REGION (16UL * 1024 * 1024)

static size_t chunk_at(const spec_region_t *r, size_t page) {
    size_t off = page * SPEC_PAGE;
    size_t rem = r->len - off;
    return rem < SPEC_PAGE ? rem : SPEC_PAGE;
}

spec_region_t *spec_region_create(void *master, size_t len) {
    if (!master || len == 0 || len > SPEC_MAX_REGION) return NULL;
    spec_region_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->master = (uint8_t *)master;
    r->len    = len;
    r->npages = (len + SPEC_PAGE - 1) / SPEC_PAGE;
    r->shadow = kmalloc(len);
    r->base   = kmalloc(len);
    if (!r->shadow || !r->base) {
        if (r->shadow) kfree(r->shadow);
        if (r->base)   kfree(r->base);
        free(r);
        return NULL;
    }
    r->state      = SPEC_IDLE;
    r->target_cpu = -1;
    return r;
}

void spec_region_destroy(spec_region_t *r) {
    if (!r) return;
    if (r->state == SPEC_RUNNING && !__atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) {
        serial_printf("[spec] destroy while worker still running -- region leaked to avoid UAF\n");
        return;
    }
    if (r->shadow) kfree(r->shadow);
    if (r->base)   kfree(r->base);
    free(r);
}

static void spec_worker_entry(void *arg) {
    spec_region_t *r = (spec_region_t *)arg;
    r->fn(r->shadow, r->len, r->arg);
    __atomic_store_n(&r->done, 1, __ATOMIC_RELEASE);
    task_exit();
}

static int spec_launch(spec_region_t *r, spec_fn_t fn, void *arg,
                       size_t read_lo, size_t read_hi, int cpu) {
    if (!r || !fn) return -1;
    if (r->state == SPEC_RUNNING) return -1;
    if (read_hi > r->len) read_hi = r->len;

    memcpy(r->base,   r->master, r->len);
    memcpy(r->shadow, r->master, r->len);

    r->fn            = fn;
    r->arg           = arg;
    r->done          = 0;
    r->worker_dirty  = 0;
    r->main_dirty    = 0;
    r->conflicts     = 0;
    r->raw_conflicts = 0;
    r->merged        = 0;
    r->read_lo       = read_lo;
    r->read_hi       = read_hi;
    r->state         = SPEC_RUNNING;

    if (cpu < 0) cpu = sched_find_free_cpu();
    uint64_t aff = (cpu >= 0) ? (1ULL << cpu) : 0;
    r->target_cpu = cpu;

    r->worker = task_create_ex("spec-worker", spec_worker_entry, r, 1, aff);
    if (!r->worker) { r->state = SPEC_IDLE; return -1; }

    if (cpu >= 0) {
        smp_info_t *si = smp_get_info();
        ipi_reschedule_cpu(si->cpus[cpu].lapic_id);
    }
    serial_printf("[spec] worker launched on cpu=%d (self=%u), region=%zu bytes / %zu pages, "
                  "read-set=[%zu..%zu)\n",
                  cpu, smp_cpu_index(), r->len, r->npages, r->read_lo, r->read_hi);
    return 0;
}

int spec_run_ex(spec_region_t *r, spec_fn_t fn, void *arg,
                size_t read_lo, size_t read_hi) {
    return spec_launch(r, fn, arg, read_lo, read_hi, -1);
}

int spec_run(spec_region_t *r, spec_fn_t fn, void *arg) {
    return spec_run_ex(r, fn, arg, 0, r ? r->len : 0);
}

int spec_poll(spec_region_t *r) {
    if (!r) return -1;
    if (!__atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) return 0;
    if (r->state == SPEC_RUNNING) r->state = SPEC_DONE;
    return 1;
}

int spec_wait(spec_region_t *r, uint32_t timeout_ms) {
    if (!r) return -1;
    uint32_t waited = 0;
    while (!__atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) {
        task_sleep_ms(1);
        if (timeout_ms && ++waited >= timeout_ms) {
            serial_printf("[spec] wait TIMEOUT after %u ms\n", timeout_ms);
            return -1;
        }
    }
    r->state = SPEC_DONE;
    return 0;
}

int spec_commit(spec_region_t *r) {
    if (!r || r->state != SPEC_DONE) return -1;

    for (size_t p = 0; p < r->npages; p++) {
        size_t off = p * SPEC_PAGE;
        size_t n   = chunk_at(r, p);
        int wdirty = memcmp(r->shadow + off, r->base   + off, n) != 0;
        int mdirty = memcmp(r->master + off, r->base   + off, n) != 0;
        int wread  = (r->read_hi > off) && (r->read_lo < off + n);
        if (wdirty) r->worker_dirty++;
        if (mdirty) r->main_dirty++;
        if (mdirty && (wdirty || wread)) {
            r->conflicts++;
            if (wread && !wdirty) r->raw_conflicts++;
        }
    }

    if (r->conflicts > 0) {
        serial_printf("[spec] COMMIT REFUSED: %llu conflict page(s) (%llu RAW read-after-write), "
                      "worker_dirty=%llu main_dirty=%llu -> caller must abort\n",
                      (unsigned long long)r->conflicts,
                      (unsigned long long)r->raw_conflicts,
                      (unsigned long long)r->worker_dirty,
                      (unsigned long long)r->main_dirty);
        return -(int)r->conflicts;
    }

    for (size_t p = 0; p < r->npages; p++) {
        size_t off = p * SPEC_PAGE;
        size_t n   = chunk_at(r, p);
        if (memcmp(r->shadow + off, r->base + off, n) != 0) {
            memcpy(r->master + off, r->shadow + off, n);
            r->merged++;
        }
    }
    r->state = SPEC_COMMITTED;
    serial_printf("[spec] COMMIT: merged %llu worker page(s), preserved %llu disjoint main page(s)\n",
                  (unsigned long long)r->merged,
                  (unsigned long long)(r->main_dirty));
    return (int)r->merged;
}

int spec_abort(spec_region_t *r) {
    if (!r) return -1;
    r->state = SPEC_ABORTED;
    serial_printf("[spec] ABORT: speculative delta discarded, master untouched\n");
    return 0;
}

typedef struct { uint8_t add; size_t touch_lo; size_t touch_hi; } spec_tf_t;

static void tf_addbyte(void *shadow, size_t len, void *arg) {
    spec_tf_t *tf = (spec_tf_t *)arg;
    uint8_t *s = (uint8_t *)shadow;
    size_t lo = tf->touch_lo, hi = tf->touch_hi > len ? len : tf->touch_hi;
    for (size_t i = lo; i < hi; i++) s[i] = (uint8_t)(s[i] + tf->add);
}

static void tf_read0_write1(void *shadow, size_t len, void *arg) {
    (void)arg;
    uint8_t *s = (uint8_t *)shadow;
    if (len < 2 * SPEC_PAGE) return;
    uint32_t acc = 0;
    for (int pass = 0; pass < 64; pass++)
        for (size_t i = 0; i < SPEC_PAGE; i++) acc += s[i];
    for (size_t i = SPEC_PAGE; i < 2 * SPEC_PAGE; i++) s[i] = (uint8_t)acc;
}

typedef struct { spec_range_fn_t fn; size_t n, base_off; void *arg; } pf_ctx_t;

static void pf_trampoline(void *shadow, size_t len, void *arg) {
    (void)len;
    pf_ctx_t *c = (pf_ctx_t *)arg;
    c->fn(shadow, c->n, c->base_off, c->arg);
}

#define SPEC_PF_MAX 8

int spec_parallel_for(void *master, size_t len, int nways,
                      spec_range_fn_t fn, void *arg) {
    if (!master || !fn || len == 0) return -1;
    if (nways < 1) nways = 1;
    if (nways > SPEC_PF_MAX) nways = SPEC_PF_MAX;

    size_t chunk = (len + (size_t)nways - 1) / (size_t)nways;
    chunk = (chunk + SPEC_PAGE - 1) & ~(SPEC_PAGE - 1);
    if (chunk == 0) chunk = SPEC_PAGE;

    spec_region_t *regs[SPEC_PF_MAX];
    pf_ctx_t       ctx[SPEC_PF_MAX];
    int launched = 0;
    uint64_t used = 0;

    for (int i = 0; i < nways; i++) {
        size_t off = (size_t)i * chunk;
        if (off >= len) break;
        size_t clen = chunk;
        if (off + clen > len) clen = len - off;
        regs[launched] = spec_region_create((uint8_t *)master + off, clen);
        if (!regs[launched]) break;
        ctx[launched].fn = fn; ctx[launched].n = clen;
        ctx[launched].base_off = off; ctx[launched].arg = arg;
        int cpu = sched_find_free_cpu_mask(used);
        if (cpu >= 0) used |= (1ULL << cpu);
        if (spec_launch(regs[launched], pf_trampoline, &ctx[launched], 0, clen, cpu) != 0) {
            spec_region_destroy(regs[launched]);
            break;
        }
        launched++;
    }

    int committed = 0, conflicts = 0;
    for (int i = 0; i < launched; i++) {
        spec_wait(regs[i], 10000);
        int c = spec_commit(regs[i]);
        if (c >= 0) committed += c; else conflicts++;
        spec_region_destroy(regs[i]);
    }

    serial_printf("[spec] parallel_for: %d chunk(s) across free cores -> "
                  "%d page(s) committed, %d conflict(s)\n",
                  launched, committed, conflicts);
    return conflicts ? -conflicts : committed;
}

static void pf_add10(void *data, size_t n, size_t base_off, void *arg) {
    (void)base_off; (void)arg;
    uint8_t *s = (uint8_t *)data;
    for (size_t i = 0; i < n; i++) s[i] = (uint8_t)(s[i] + 10);
}

int spec_selftest2(void) {
    const size_t NP  = 4;
    const size_t LEN = NP * SPEC_PAGE;
    int score = 0;

    uint8_t *region = kmalloc(LEN);
    if (!region) { serial_printf("[spec] selftest: OOM\n"); return -1; }
    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);

    serial_printf("[spec] === selftest2: speculative commit/abort engine ===\n");

    spec_region_t *r = spec_region_create(region, LEN);
    if (!r) { kfree(region); return -1; }

    spec_tf_t all = { .add = 1, .touch_lo = 0, .touch_hi = LEN };
    if (spec_run(r, tf_addbyte, &all) == 0 && spec_wait(r, 5000) == 0) {
        if (spec_commit(r) == (int)NP) {
            int ok = 1;
            for (size_t i = 0; i < LEN && ok; i++)
                if (region[i] != (uint8_t)(((i & 0xFF) + 1) & 0xFF)) ok = 0;
            serial_printf("[spec] test1 COMMIT: %s (all %zu bytes +1)\n",
                          ok ? "PASS" : "FAIL", LEN);
            if (ok) score |= 1;
        }
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    spec_tf_t garb = { .add = 0x55, .touch_lo = 0, .touch_hi = LEN };
    if (spec_run(r, tf_addbyte, &garb) == 0 && spec_wait(r, 5000) == 0) {
        spec_abort(r);
        int ok = 1;
        for (size_t i = 0; i < LEN && ok; i++)
            if (region[i] != (uint8_t)(i & 0xFF)) ok = 0;
        serial_printf("[spec] test2 ABORT: %s (master unchanged)\n", ok ? "PASS" : "FAIL");
        if (ok) score |= 2;
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    spec_tf_t wp0 = { .add = 1, .touch_lo = 0, .touch_hi = SPEC_PAGE };
    if (spec_run(r, tf_addbyte, &wp0) == 0) {
        for (size_t i = 0; i < SPEC_PAGE; i++) region[i] = 0xAA;
        if (spec_wait(r, 5000) == 0) {
            int c = spec_commit(r);
            int ok = (c < 0);
            serial_printf("[spec] test3 CONFLICT: %s (commit refused rc=%d)\n",
                          ok ? "PASS" : "FAIL", c);
            if (ok) { spec_abort(r); score |= 4; }
        }
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    spec_tf_t wlo = { .add = 3, .touch_lo = 0, .touch_hi = SPEC_PAGE };
    if (spec_run_ex(r, tf_addbyte, &wlo, 0, SPEC_PAGE) == 0) {
        for (size_t i = 3 * SPEC_PAGE; i < LEN; i++) region[i] = 0x77;
        if (spec_wait(r, 5000) == 0) {
            int c = spec_commit(r);
            int ok = (c == 1);
            for (size_t i = 0; i < SPEC_PAGE && ok; i++)
                if (region[i] != (uint8_t)(((i & 0xFF) + 3) & 0xFF)) ok = 0;
            for (size_t i = 3 * SPEC_PAGE; i < LEN && ok; i++)
                if (region[i] != 0x77) ok = 0;
            serial_printf("[spec] test4 PARALLEL DISJOINT: %s (worker p0 + main p3 coexist, rc=%d)\n",
                          ok ? "PASS" : "FAIL", c);
            if (ok) score |= 8;
        }
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    if (spec_run_ex(r, tf_read0_write1, NULL, 0, SPEC_PAGE) == 0) {
        for (size_t i = 0; i < SPEC_PAGE; i++) region[i] = 0xAA;
        if (spec_wait(r, 5000) == 0) {
            int c = spec_commit(r);
            int ok = (c < 0 && r->raw_conflicts >= 1);
            for (size_t i = 0; i < SPEC_PAGE && ok; i++)
                if (region[i] != 0xAA) ok = 0;
            serial_printf("[spec] test5 RAW CONFLICT: %s (worker read p0, main wrote p0 -> refused rc=%d raw=%llu)\n",
                          ok ? "PASS" : "FAIL", c, (unsigned long long)r->raw_conflicts);
            if (ok) { spec_abort(r); score |= 16; }
        }
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    uint64_t parent_iters = 0;
    if (spec_run_ex(r, tf_read0_write1, NULL, 0, SPEC_PAGE) == 0) {
        uint32_t guard = 0;
        while (!spec_poll(r)) {
            for (size_t i = 3 * SPEC_PAGE; i < LEN; i++)
                region[i] = (uint8_t)(region[i] + 1);
            parent_iters++;
            task_sleep_ms(1);
            if (++guard > 5000) break;
        }
        int c = spec_commit(r);
        int ok = (c >= 1 && parent_iters >= 1);
        serial_printf("[spec] test6 NONBLOCK PARALLEL: %s (parent did %llu concurrent iters while worker ran, rc=%d)\n",
                      ok ? "PASS" : "FAIL", (unsigned long long)parent_iters, c);
        if (ok) score |= 32;
    }

    for (size_t i = 0; i < LEN; i++) region[i] = (uint8_t)(i & 0xFF);
    {
        int c = spec_parallel_for(region, LEN, 4, pf_add10, NULL);
        int ok = (c >= 1);
        for (size_t i = 0; i < LEN && ok; i++)
            if (region[i] != (uint8_t)(((i & 0xFF) + 10) & 0xFF)) ok = 0;
        serial_printf("[spec] test7 PARALLEL-FOR: %s (auto-split into 4 chunks across cores, rc=%d)\n",
                      ok ? "PASS" : "FAIL", c);
        if (ok) score |= 64;
    }

    serial_printf("[spec] === selftest2 score=0x%x (%s) ===\n",
                  score, score == 0x7F ? "ALL PASS" : "SOME FAILED");
    spec_region_destroy(r);
    kfree(region);
    return score;
}
