#include "../../../include/drivers/pci.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/apic/apic.h"
#include "../../../include/memory/dma.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/sched/sched.h"
#include "../../../include/smp/percpu.h"
#include "../../../include/firmware/dmi.h"
#include <stdio.h>

static inline void *sys_zalloc(size_t n) { return kzalloc(n); }
static inline void  sys_free(void *p)    { kfree(p); }
static inline void  sys_pci_register(const pci_driver_t *d) { pci_register_driver(d); }

#include "kpi_pci.h"

#define KPI_IRQ_PRIO   IRQ_THREAD_PRIORITY
#define KPI_NAPI_PRIO  19
#define KPI_MAX_BIND   8
#define KPI_MAX_IDS    32
#define KPI_MAX_PENDING 8
#define KPI_MAX_DEVS   8

struct kpi_binding {
    struct pci_driver *ldrv;
    pci_driver_t       cdrv;
    uint32_t           ids[KPI_MAX_IDS];
    char               name[24];
};

struct kpi_pending {
    struct kpi_binding *b;
    pci_device_t       *pd;
};

static struct kpi_binding g_bind[KPI_MAX_BIND];
static int                g_nbind;
static struct kpi_pending g_pending[KPI_MAX_PENDING];
static int                g_npending;
static struct pci_dev    *g_devs[KPI_MAX_DEVS];
static int                g_ndevs;
static volatile int       g_started;

int kpi_pci_read_config_byte(struct pci_dev *p, int off, u8 *v)
{
    pci_device_t *d = p->pd;
    *v = pci_config_read8(d->segment, d->bus, d->device, d->function, (uint16_t)off);
    return 0;
}

int kpi_pci_read_config_word(struct pci_dev *p, int off, u16 *v)
{
    pci_device_t *d = p->pd;
    *v = pci_config_read16(d->segment, d->bus, d->device, d->function, (uint16_t)off);
    return 0;
}

int kpi_pci_read_config_dword(struct pci_dev *p, int off, u32 *v)
{
    pci_device_t *d = p->pd;
    *v = pci_config_read32(d->segment, d->bus, d->device, d->function, (uint16_t)off);
    return 0;
}

int kpi_pci_write_config_byte(struct pci_dev *p, int off, u8 v)
{
    pci_device_t *d = p->pd;
    pci_config_write8(d->segment, d->bus, d->device, d->function, (uint16_t)off, v);
    return 0;
}

int kpi_pci_write_config_word(struct pci_dev *p, int off, u16 v)
{
    pci_device_t *d = p->pd;
    pci_config_write16(d->segment, d->bus, d->device, d->function, (uint16_t)off, v);
    return 0;
}

int kpi_pci_write_config_dword(struct pci_dev *p, int off, u32 v)
{
    pci_device_t *d = p->pd;
    pci_config_write32(d->segment, d->bus, d->device, d->function, (uint16_t)off, v);
    return 0;
}

int kpi_pcie_capability_read_word(struct pci_dev *p, int pos, u16 *v)
{
    *v = 0;
    if (!p->pd->cap_exp_off) return -EINVAL;
    return kpi_pci_read_config_word(p, p->pd->cap_exp_off + pos, v);
}

int kpi_pcie_capability_write_word(struct pci_dev *p, int pos, u16 v)
{
    if (!p->pd->cap_exp_off) return -EINVAL;
    return kpi_pci_write_config_word(p, p->pd->cap_exp_off + pos, v);
}

int kpi_pci_enable_device(struct pci_dev *p)
{
    pci_power_up(p->pd);
    u16 cmd;
    kpi_pci_read_config_word(p, PCI_COMMAND, &cmd);
    cmd |= 0x0002;
    for (int i = 0; i < 6; i++)
        if (p->pd->bars[i].type == PCI_BAR_TYPE_IO && p->pd->bars[i].base) cmd |= 0x0001;
    kpi_pci_write_config_word(p, PCI_COMMAND, cmd);
    return 0;
}

void kpi_pci_set_master(struct pci_dev *p)
{
    u16 cmd;
    kpi_pci_read_config_word(p, PCI_COMMAND, &cmd);
    if (!(cmd & PCI_COMMAND_MASTER))
        kpi_pci_write_config_word(p, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
}

resource_size_t kpi_pci_resource_len(struct pci_dev *p, int bar)
{
    if (bar < 0 || bar >= 6) return 0;
    return p->pd->bars[bar].size;
}

resource_size_t kpi_pci_resource_start(struct pci_dev *p, int bar)
{
    if (bar < 0 || bar >= 6) return 0;
    return p->pd->bars[bar].base;
}

unsigned long kpi_pci_resource_flags(struct pci_dev *p, int bar)
{
    if (bar < 0 || bar >= 6 || !p->pd->bars[bar].base) return 0;
    return p->pd->bars[bar].type == PCI_BAR_TYPE_IO ? IORESOURCE_IO : IORESOURCE_MEM;
}

void __iomem *kpi_pci_iomap(struct pci_dev *p, int bar, unsigned long len)
{
    if (bar < 0 || bar >= 6) return NULL;
    pci_bar_t *b = &p->pd->bars[bar];
    if (!b->base || b->type != PCI_BAR_TYPE_MEM) return NULL;
    if (!len || len > b->size) len = (unsigned long)b->size;
    return (void __iomem *)mmio_map((uintptr_t)b->base, len);
}

int kpi_pci_alloc_irq_vectors(struct pci_dev *p, int min, int max, unsigned int flags)
{
    (void)max;
    if (min > 1) return -ENOSPC;
    p->irq_vector = -1;
    if ((flags & PCI_IRQ_MSI) && p->pd->cap_msi_off) {
        int vec = irq_alloc_vector();
        if (vec > 0) {
            p->irq_vector = vec;
            p->irq = (unsigned int)vec;
            return 1;
        }
    }
    if (flags & PCI_IRQ_INTX) {
        p->irq = 0;
        return 1;
    }
    return -ENOSPC;
}

void kpi_pci_free_irq_vectors(struct pci_dev *p)
{
    if (p->irq_vector > 0) irq_free_vector(p->irq_vector);
    p->irq_vector = -1;
    p->irq = 0;
}

static void kpi_irq_entry(void *ctx)
{
    struct pci_dev *p = ctx;
    __atomic_store_n(&p->irq_hard_running, 1, __ATOMIC_RELEASE);
    irqreturn_t r = p->irq_handler ? p->irq_handler((int)p->irq, p->irq_ctx) : IRQ_WAKE_THREAD;
    __atomic_store_n(&p->irq_hard_running, 0, __ATOMIC_RELEASE);
    if (r == IRQ_WAKE_THREAD && p->irq_thread) {
        __atomic_store_n(&p->irq_thread_pending, 1, __ATOMIC_RELEASE);
        kpi_wake_up(&p->irq_wait);
    }
}

static void kpi_irq_thread(void *arg)
{
    struct pci_dev *p = arg;
    while (!p->irq_stop) {
        u32 seq = __atomic_load_n(&p->irq_wait.seq, __ATOMIC_ACQUIRE);
        if (p->irq_polled && !p->irq_disabled) {
            irqreturn_t r = p->irq_handler ? p->irq_handler((int)p->irq, p->irq_ctx) : IRQ_WAKE_THREAD;
            if (r == IRQ_WAKE_THREAD) p->irq_thread_pending = 1;
        }
        if (__atomic_exchange_n(&p->irq_thread_pending, 0, __ATOMIC_ACQ_REL)) {
            __atomic_store_n(&p->irq_thread_running, 1, __ATOMIC_RELEASE);
            if (p->irq_thread) p->irq_thread((int)p->irq, p->irq_ctx);
            __atomic_store_n(&p->irq_thread_running, 0, __ATOMIC_RELEASE);
            kpi_wake_up(&p->irq_idle);
            continue;
        }
        kpi_wake_up(&p->irq_idle);
        kpi_wait_on(&p->irq_wait, seq, p->irq_polled ? 1000000ULL : 1000000000ULL);
    }
    p->irq_task = NULL;
    task_exit();
}

int kpi_request_threaded_irq(struct pci_dev *p, irqreturn_t (*h)(int, void *),
                             irqreturn_t (*t)(int, void *), void *ctx)
{
    p->irq_handler = h;
    p->irq_thread = t;
    p->irq_ctx = ctx;
    p->irq_stop = 0;
    p->irq_disabled = 0;
    p->irq_thread_pending = 0;
    kpi_waitq_init(&p->irq_wait);
    kpi_waitq_init(&p->irq_idle);
    p->irq_polled = p->irq_vector <= 0;
    p->irq_task = task_create(p->irq_name, kpi_irq_thread, p, KPI_IRQ_PRIO);
    if (!p->irq_task) return -ENOMEM;
    if (!p->irq_polled) {
        if (pci_enable_msi(p->pd, (uint8_t)p->irq_vector, lapic_get_id()) != 0 ||
            irq_request(p->irq_vector, kpi_irq_entry, p, p->irq_name) != 0) {
            irq_free_vector(p->irq_vector);
            p->irq_vector = -1;
            p->irq = 0;
            p->irq_polled = 1;
        }
    }
    serial_printf_lvl(LOG_LEVEL_INFO, "%s: interrupts via %s\n", p->dev.name,
                      p->irq_polled ? "polling" : "msi");
    return 0;
}

void kpi_synchronize_irq(struct pci_dev *p)
{
    for (;;) {
        u32 seq = __atomic_load_n(&p->irq_idle.seq, __ATOMIC_ACQUIRE);
        if (!p->irq_hard_running && !p->irq_thread_running && !p->irq_thread_pending) return;
        if (!p->irq_task) return;
        kpi_wait_on(&p->irq_idle, seq, 5000000ULL);
    }
}

static struct pci_dev *dev_by_irq(unsigned int irq)
{
    for (int i = 0; i < g_ndevs; i++)
        if (g_devs[i] && g_devs[i]->irq == irq) return g_devs[i];
    return NULL;
}

void kpi_synchronize_irq_num(unsigned int irq)
{
    struct pci_dev *p = dev_by_irq(irq);
    if (p) kpi_synchronize_irq(p);
}

void kpi_disable_irq_num(unsigned int irq)
{
    struct pci_dev *p = dev_by_irq(irq);
    if (!p) return;
    p->irq_disabled = 1;
    kpi_synchronize_irq(p);
}

void kpi_enable_irq_num(unsigned int irq)
{
    struct pci_dev *p = dev_by_irq(irq);
    if (p) p->irq_disabled = 0;
}

void kpi_free_irq(struct pci_dev *p)
{
    if (p->irq_vector > 0) irq_free(p->irq_vector);
    p->irq_stop = 1;
    kpi_wake_up(&p->irq_wait);
    for (int i = 0; i < 1000 && p->irq_task; i++) kpi_msleep(1);
}

struct net_device *kpi_alloc_netdev_dummy(void)
{
    return sys_zalloc(sizeof(struct net_device));
}

void kpi_free_netdev(struct net_device *d)
{
    sys_free(d);
}

static struct workqueue_struct *g_napi_wq;

#define NAPI_SCHED  1
#define NAPI_MISSED 2

static void napi_work(struct work_struct *w)
{
    struct napi_struct *n = container_of(w, struct napi_struct, work);
    if (!n->enabled) {
        __atomic_and_fetch(&n->state, ~(NAPI_SCHED | NAPI_MISSED), __ATOMIC_ACQ_REL);
        return;
    }
    int done = n->poll(n, n->weight);
    if (done >= n->weight && (__atomic_load_n(&n->state, __ATOMIC_ACQUIRE) & NAPI_SCHED))
        kpi_queue_work(g_napi_wq, &n->work);
}

void kpi_netif_napi_add(struct net_device *d, struct napi_struct *n, napi_poll_fn poll)
{
    if (!g_napi_wq) g_napi_wq = kpi_alloc_workqueue("napi");
    n->dev = d;
    n->poll = poll;
    n->weight = NAPI_POLL_WEIGHT;
    n->state = 0;
    n->enabled = 0;
    kpi_init_work(&n->work, napi_work);
}

void kpi_napi_schedule(struct napi_struct *n)
{
    if (!n->enabled) return;
    int st = __atomic_load_n(&n->state, __ATOMIC_ACQUIRE);
    for (;;) {
        int nst = (st & NAPI_SCHED) ? (st | NAPI_MISSED) : (st | NAPI_SCHED);
        if (__atomic_compare_exchange_n(&n->state, &st, nst, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            if (!(st & NAPI_SCHED)) kpi_queue_work(g_napi_wq, &n->work);
            return;
        }
    }
}

bool kpi_napi_complete_done(struct napi_struct *n, int done)
{
    (void)done;
    int st = __atomic_load_n(&n->state, __ATOMIC_ACQUIRE);
    for (;;) {
        if (st & NAPI_MISSED) {
            if (__atomic_compare_exchange_n(&n->state, &st, st & ~NAPI_MISSED, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                kpi_queue_work(g_napi_wq, &n->work);
                return false;
            }
            continue;
        }
        if (__atomic_compare_exchange_n(&n->state, &st, st & ~NAPI_SCHED, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

void kpi_napi_enable(struct napi_struct *n)
{
    __atomic_store_n(&n->state, 0, __ATOMIC_RELEASE);
    n->enabled = 1;
}

void kpi_napi_disable(struct napi_struct *n)
{
    n->enabled = 0;
    kpi_cancel_work_sync(&n->work);
    __atomic_store_n(&n->state, 0, __ATOMIC_RELEASE);
}

void kpi_napi_synchronize(struct napi_struct *n)
{
    kpi_flush_work(&n->work);
}

static bool dmi_one(const struct dmi_strmatch *m)
{
    const char *v = dmi_get_system_info(m->slot);
    if (!v) return false;
    if (m->exact_match) return strcmp(v, m->substr) == 0;
    return strstr(v, m->substr) != NULL;
}

int kpi_dmi_check_system(const struct dmi_system_id *list)
{
    int count = 0;
    if (!dmi_available()) return 0;
    for (const struct dmi_system_id *d = list; d->matches[0].slot || d->callback || d->ident; d++) {
        bool ok = true;
        for (int i = 0; i < 4 && d->matches[i].slot; i++)
            if (!dmi_one(&d->matches[i])) { ok = false; break; }
        if (!ok) continue;
        count++;
        serial_printf_lvl(LOG_LEVEL_INFO, "dmi: quirk matched: %s\n", d->ident ? d->ident : "?");
        if (d->callback && d->callback(d)) break;
    }
    return count;
}

static const struct pci_device_id *match_id(const struct pci_device_id *t, pci_device_t *pd,
                                            u16 sv, u16 sd)
{
    for (; t->vendor || t->device || t->subvendor || t->class_; t++) {
        if (t->vendor != PCI_ANY_ID && t->vendor != pd->vendor_id) continue;
        if (t->device != PCI_ANY_ID && t->device != pd->device_id) continue;
        if (t->subvendor != PCI_ANY_ID && t->subvendor != 0 && t->subvendor != sv) continue;
        if (t->subdevice != PCI_ANY_ID && t->subdevice != 0 && t->subdevice != sd) continue;
        if (t->class_mask) {
            u32 cls = ((u32)pd->class_code << 16) | ((u32)pd->subclass << 8) | pd->prog_if;
            if ((cls & t->class_mask) != (t->class_ & t->class_mask)) continue;
        }
        return t;
    }
    return NULL;
}

static int claim(pci_device_t *pd, void *ctx)
{
    struct kpi_binding *b = ctx;
    if (g_npending >= KPI_MAX_PENDING) return -1;
    u16 sv = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function, 0x2C);
    u16 sd = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function, 0x2E);
    if (!match_id(b->ldrv->id_table, pd, sv, sd)) return -1;
    for (int i = 0; i < g_npending; i++)
        if (g_pending[i].pd == pd) return 0;
    g_pending[g_npending].b = b;
    g_pending[g_npending].pd = pd;
    g_npending++;
    return 0;
}

int kpi_pci_register_driver(struct pci_driver *drv)
{
    if (!drv || g_nbind >= KPI_MAX_BIND) return -ENOSPC;
    struct kpi_binding *b = &g_bind[g_nbind++];
    b->ldrv = drv;
    strncpy(b->name, drv->name ? drv->name : "linux", sizeof(b->name) - 1);
    int n = 0;
    for (const struct pci_device_id *t = drv->id_table; t->vendor || t->device; t++) {
        if (t->vendor == PCI_ANY_ID || t->device == PCI_ANY_ID) continue;
        if (n < KPI_MAX_IDS) b->ids[n++] = (t->vendor << 16) | (t->device & 0xffff);
    }
    b->cdrv.name = b->name;
    b->cdrv.match_vendor = -1;
    b->cdrv.match_device = -1;
    b->cdrv.match_class = -1;
    b->cdrv.match_subclass = -1;
    b->cdrv.match_ids = b->ids;
    b->cdrv.match_id_count = n;
    b->cdrv.probe_ctx = claim;
    b->cdrv.ctx = b;
    sys_pci_register(&b->cdrv);
    return 0;
}

static void probe_one(struct kpi_pending *pp)
{
    pci_device_t *pd = pp->pd;
    struct pci_driver *ldrv = pp->b->ldrv;
    struct pci_dev *p = sys_zalloc(sizeof(*p));
    if (!p) return;
    p->pd = pd;
    p->vendor = pd->vendor_id;
    p->device = pd->device_id;
    p->revision = pd->revision;
    p->subsystem_vendor = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function, 0x2C);
    p->subsystem_device = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function, 0x2E);
    p->irq_vector = -1;
    p->dev.dma_mask = DMA_BIT_MASK(32);
    snprintf(p->dev_name, sizeof(p->dev_name), "%04x:%02x:%02x.%u",
             pd->segment, pd->bus, pd->device, pd->function);
    p->dev.name = p->dev_name;
    p->dev.driver_name = ldrv->name;
    snprintf(p->irq_name, sizeof(p->irq_name), "irq/%s", ldrv->name ? ldrv->name : "kpi");
    if (g_ndevs < KPI_MAX_DEVS) g_devs[g_ndevs++] = p;
    const struct pci_device_id *id = match_id(ldrv->id_table, pd, p->subsystem_vendor, p->subsystem_device);
    serial_printf_lvl(LOG_LEVEL_INFO, "%s: probing %04x:%04x rev %02x at %s\n",
                      ldrv->name, pd->vendor_id, pd->device_id, pd->revision, p->dev_name);
    int r = ldrv->probe(p, id);
    if (r) {
        serial_printf_lvl(LOG_LEVEL_ERR, "%s: probe of %s failed: %d\n", ldrv->name, p->dev_name, r);
        printf("%s: %04x:%04x at %s: attach failed (%d)\n", ldrv->name,
               pd->vendor_id, pd->device_id, p->dev_name, r);
    }
}

static void probe_thread(void *arg)
{
    (void)arg;
    for (int i = 0; i < g_npending; i++) probe_one(&g_pending[i]);
    task_exit();
}

void kpi_pci_start(void)
{
    if (g_started || !g_npending) return;
    g_started = 1;
    kpi_runtime_init();
    task_create("kpi_probe", probe_thread, NULL, 16);
}
