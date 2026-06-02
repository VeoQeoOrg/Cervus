#include "../../../include/drivers/disk/nvme.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/drivers/disk/blkdev.h"
#include "../../../include/memory/dma.h"
#include "../../../include/memory/pmm.h"
#include "../../../include/memory/vmm.h"
#include "../../../include/memory/paging.h"
#include "../../../include/apic/apic.h"
#include "../../../include/interrupts/irq.h"
#include "../../../include/io/serial.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/sched/spinlock.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define NVME_CAP     0x00
#define NVME_VS      0x08
#define NVME_INTMS   0x0C
#define NVME_INTMC   0x10
#define NVME_CC      0x14
#define NVME_CSTS    0x1C
#define NVME_AQA     0x24
#define NVME_ASQ     0x28
#define NVME_ACQ     0x30

#define CC_EN        (1u << 0)
#define CC_CSS_NVM   (0u << 4)
#define CC_AMS_RR    (0u << 11)
#define CC_SHN_NONE  (0u << 14)
#define CC_IOSQES_64 (6u << 16)
#define CC_IOCQES_16 (4u << 20)

#define CSTS_RDY     (1u << 0)
#define CSTS_CFS     (1u << 1)

#define NVME_ADMIN_QUEUE_DEPTH 64
#define NVME_IO_QUEUE_DEPTH    64
#define NVME_QUEUE_ID_IO       1

#define NVME_OPC_DELETE_IO_SQ 0x00
#define NVME_OPC_CREATE_IO_SQ 0x01
#define NVME_OPC_DELETE_IO_CQ 0x04
#define NVME_OPC_CREATE_IO_CQ 0x05
#define NVME_OPC_IDENTIFY     0x06
#define NVME_OPC_SET_FEATURES 0x09

#define NVME_NVM_FLUSH 0x00
#define NVME_NVM_WRITE 0x01
#define NVME_NVM_READ  0x02

#define NVME_FEAT_NUM_QUEUES 0x07

#define NVME_MAX_XFER_BYTES (128 * 1024)

typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t rsv;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_cmd_t;

_Static_assert(sizeof(nvme_cmd_t) == 64, "nvme_cmd_t size");

typedef struct __attribute__((packed)) {
    uint32_t cmd_specific;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_completion_t;

_Static_assert(sizeof(nvme_completion_t) == 16, "completion size");

struct nvme_controller {
    pci_device_t *pdev;
    volatile uint8_t *bar0;
    uint32_t  doorbell_stride;
    uint16_t  mqes;
    bool      dead;

    void     *adm_sq_virt;
    void     *adm_cq_virt;
    uintptr_t adm_sq_phys;
    uintptr_t adm_cq_phys;
    uint16_t  adm_sq_tail;
    uint16_t  adm_cq_head;
    uint8_t   adm_cq_phase;
    uint16_t  adm_depth;

    void     *io_sq_virt;
    void     *io_cq_virt;
    uintptr_t io_sq_phys;
    uintptr_t io_cq_phys;
    uint16_t  io_sq_tail;
    uint16_t  io_cq_head;
    uint8_t   io_cq_phase;
    uint16_t  io_depth;

    char model[41];
    char serial_no[21];
    uint8_t  mdts;
    uint32_t max_xfer_bytes;

    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
    int      ns_count;

    int      irq_vector;
    bool     irq_enabled;

    spinlock_t adm_lock;
    spinlock_t io_lock;
};

static nvme_controller_t g_controllers[NVME_MAX_CONTROLLERS];
static int g_ctrl_count = 0;
static nvme_namespace_t *g_namespaces[NVME_MAX_CONTROLLERS * NVME_MAX_NAMESPACES];
static int g_ns_count = 0;

static inline uint32_t reg_r32(nvme_controller_t *c, uint32_t off) {
    return *(volatile uint32_t *)(c->bar0 + off);
}
static inline void reg_w32(nvme_controller_t *c, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(c->bar0 + off) = v;
}
static inline uint64_t reg_r64(nvme_controller_t *c, uint32_t off) {
    uint64_t lo = reg_r32(c, off);
    uint64_t hi = reg_r32(c, off + 4);
    return lo | (hi << 32);
}
static inline void reg_w64(nvme_controller_t *c, uint32_t off, uint64_t v) {
    reg_w32(c, off,     (uint32_t)(v & 0xFFFFFFFFu));
    reg_w32(c, off + 4, (uint32_t)(v >> 32));
}

static inline volatile uint32_t *sq_doorbell(nvme_controller_t *c, uint16_t qid) {
    return (volatile uint32_t *)(c->bar0 + 0x1000 + (uint32_t)(2 * qid) * c->doorbell_stride);
}
static inline volatile uint32_t *cq_doorbell(nvme_controller_t *c, uint16_t qid) {
    return (volatile uint32_t *)(c->bar0 + 0x1000 + (uint32_t)(2 * qid + 1) * c->doorbell_stride);
}

static int nvme_submit_sync(nvme_controller_t *c, bool is_admin, nvme_cmd_t *cmd,
                            nvme_completion_t *cpl_out) {
    if (c->dead) return -EIO;

    nvme_cmd_t *sq;
    nvme_completion_t *cq;
    uint16_t *tail_p;
    uint16_t *head_p;
    uint8_t  *phase_p;
    uint16_t  depth;
    uint16_t  qid;

    if (is_admin) {
        sq = (nvme_cmd_t *)c->adm_sq_virt;
        cq = (nvme_completion_t *)c->adm_cq_virt;
        tail_p = &c->adm_sq_tail;
        head_p = &c->adm_cq_head;
        phase_p = &c->adm_cq_phase;
        depth = c->adm_depth;
        qid = 0;
    } else {
        sq = (nvme_cmd_t *)c->io_sq_virt;
        cq = (nvme_completion_t *)c->io_cq_virt;
        tail_p = &c->io_sq_tail;
        head_p = &c->io_cq_head;
        phase_p = &c->io_cq_phase;
        depth = c->io_depth;
        qid = NVME_QUEUE_ID_IO;
    }

    uint16_t slot = *tail_p;
    cmd->cid = slot;
    memcpy(&sq[slot], cmd, sizeof(*cmd));

    uint16_t new_tail = (uint16_t)(slot + 1);
    if (new_tail >= depth) new_tail = 0;
    *tail_p = new_tail;

    asm volatile("" ::: "memory");
    *sq_doorbell(c, qid) = new_tail;

    uint16_t head = *head_p;
    volatile nvme_completion_t *e = &cq[head];

    uint64_t timeout_ns = (cmd->opcode == NVME_NVM_FLUSH)
                        ? 30000000000ULL
                        : 10000000000ULL;
    uint64_t start_ns = hpet_elapsed_ns();

    for (;;) {
        uint16_t st = e->status;
        if ((st & 1) == *phase_p) {
            nvme_completion_t snap;
            memcpy(&snap, (const void *)e, sizeof(snap));
            if (cpl_out) *cpl_out = snap;

            uint16_t new_head = (uint16_t)(head + 1);
            if (new_head >= depth) {
                new_head = 0;
                *phase_p ^= 1;
            }
            *head_p = new_head;
            *cq_doorbell(c, qid) = new_head;

            uint16_t sc  = (snap.status >> 1) & 0xFF;
            uint16_t sct = (snap.status >> 9) & 0x7;
            if (sc != 0 || sct != 0) {
                serial_printf("[nvme] opcode 0x%02x failed: sct=%u sc=0x%02x\n",
                              cmd->opcode, sct, sc);
                return -EIO;
            }
            return 0;
        }
        if (hpet_elapsed_ns() - start_ns > timeout_ns) break;
        asm volatile("pause");
    }

    uint64_t elapsed_ms = (hpet_elapsed_ns() - start_ns) / 1000000ULL;
    serial_printf("[nvme] opcode 0x%02x TIMEOUT after %llu ms — marking controller dead\n",
                  cmd->opcode, (unsigned long long)elapsed_ms);
    c->dead = true;
    return -ETIMEDOUT;
}

static void nvme_irq_handler(void *ctx) {
    (void)ctx;
}

static void trim_trailing(char *s, size_t n) {
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\0')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int nvme_identify_controller(nvme_controller_t *c) {
    uintptr_t buf_phys;
    void *buf = dma_alloc_coherent(4096, &buf_phys);
    if (!buf) return -ENOMEM;

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_IDENTIFY;
    cmd.prp1   = buf_phys;
    cmd.cdw10  = 0x01;

    int r = nvme_submit_sync(c, true, &cmd, NULL);
    if (r < 0) { dma_free_coherent(buf, 4096); return r; }

    uint8_t *id = (uint8_t *)buf;
    memcpy(c->serial_no, id + 4, 20);
    c->serial_no[20] = '\0';
    trim_trailing(c->serial_no, 20);
    memcpy(c->model, id + 24, 40);
    c->model[40] = '\0';
    trim_trailing(c->model, 40);
    c->mdts = id[77];

    dma_free_coherent(buf, 4096);
    return 0;
}

static int nvme_set_num_queues(nvme_controller_t *c, uint16_t requested) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_SET_FEATURES;
    cmd.cdw10  = NVME_FEAT_NUM_QUEUES;
    cmd.cdw11  = ((uint32_t)(requested - 1) << 16) | (uint32_t)(requested - 1);
    return nvme_submit_sync(c, true, &cmd, NULL);
}

static int nvme_create_io_cq(nvme_controller_t *c) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_CREATE_IO_CQ;
    cmd.prp1   = c->io_cq_phys;
    cmd.cdw10  = ((uint32_t)(c->io_depth - 1) << 16) | NVME_QUEUE_ID_IO;
    uint32_t ien = c->irq_enabled ? 2u : 0u;
    cmd.cdw11  = 1u | ien | (0u << 16);
    return nvme_submit_sync(c, true, &cmd, NULL);
}

static int nvme_create_io_sq(nvme_controller_t *c) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_CREATE_IO_SQ;
    cmd.prp1   = c->io_sq_phys;
    cmd.cdw10  = ((uint32_t)(c->io_depth - 1) << 16) | NVME_QUEUE_ID_IO;
    cmd.cdw11  = 1u | ((uint32_t)NVME_QUEUE_ID_IO << 16);
    return nvme_submit_sync(c, true, &cmd, NULL);
}

static int nvme_identify_namespace(nvme_controller_t *c, uint32_t nsid, void *out4k) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_IDENTIFY;
    cmd.nsid   = nsid;
    cmd.prp1   = pmm_virt_to_phys(out4k);
    cmd.cdw10  = 0x00;
    return nvme_submit_sync(c, true, &cmd, NULL);
}

static int nvme_get_active_ns_list(nvme_controller_t *c, void *out4k) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OPC_IDENTIFY;
    cmd.prp1   = pmm_virt_to_phys(out4k);
    cmd.cdw10  = 0x02;
    return nvme_submit_sync(c, true, &cmd, NULL);
}

static int nvme_io_one(nvme_controller_t *c, nvme_namespace_t *ns, bool write,
                       uint64_t lba, uint16_t nblocks, uintptr_t buf_phys, uint32_t bytes) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = write ? NVME_NVM_WRITE : NVME_NVM_READ;
    cmd.nsid   = ns->nsid;
    cmd.prp1   = buf_phys;

    uintptr_t prp_list_phys = 0;
    void *prp_list_virt = NULL;

    uint32_t first_page_bytes = 4096u - (uint32_t)(buf_phys & 0xFFFu);
    if (bytes <= first_page_bytes) {
        cmd.prp2 = 0;
    } else {
        uint32_t remaining = bytes - first_page_bytes;
        uint32_t extra_pages = (remaining + 4095u) / 4096u;
        if (extra_pages == 1) {
            cmd.prp2 = (buf_phys & ~0xFFFULL) + 4096ULL;
        } else {
            prp_list_virt = dma_alloc_coherent(4096, &prp_list_phys);
            if (!prp_list_virt) return -ENOMEM;
            uint64_t *list = (uint64_t *)prp_list_virt;
            uintptr_t p = (buf_phys & ~0xFFFULL) + 4096ULL;
            for (uint32_t i = 0; i < extra_pages; i++) {
                list[i] = (uint64_t)p;
                p += 4096ULL;
            }
            cmd.prp2 = (uint64_t)prp_list_phys;
        }
    }

    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (uint32_t)(nblocks - 1);

    int r = nvme_submit_sync(c, false, &cmd, NULL);

    if (r == -ETIMEDOUT) {
        if (prp_list_virt) {
            serial_printf("[nvme] leaking prp list page after timeout (DMA may still be active)\n");
        }
        return r;
    }

    if (prp_list_virt) dma_free_coherent(prp_list_virt, 4096);
    return r;
}

static int nvme_rw(nvme_namespace_t *ns, uint64_t lba, uint32_t count, void *buf, bool write) {
    if (!ns || !ns->active || !buf || count == 0) return -EINVAL;
    nvme_controller_t *c = ns->ctrl;

    uint32_t lba_size = ns->lba_size;
    uint32_t max_xfer = c->max_xfer_bytes ? c->max_xfer_bytes : NVME_MAX_XFER_BYTES;
    if (max_xfer > NVME_MAX_XFER_BYTES) max_xfer = NVME_MAX_XFER_BYTES;
    uint32_t max_blocks = max_xfer / lba_size;
    if (max_blocks == 0) return -EINVAL;

    spinlock_acquire(&c->io_lock);

    uint8_t *bp = (uint8_t *)buf;
    uint64_t cur = lba;
    uint32_t left = count;

    while (left > 0) {
        uint32_t chunk = left > max_blocks ? max_blocks : left;
        uint32_t chunk_bytes = chunk * lba_size;
        size_t   alloc_bytes = (chunk_bytes + 4095u) & ~4095u;

        uintptr_t bphys;
        void *bounce = dma_alloc_coherent(alloc_bytes, &bphys);
        if (!bounce) { spinlock_release(&c->io_lock); return -ENOMEM; }

        if (write) memcpy(bounce, bp, chunk_bytes);

        int r = nvme_io_one(c, ns, write, cur, (uint16_t)chunk, bphys, chunk_bytes);
        if (r != 0) {
            if (r == -ETIMEDOUT) {
                serial_printf("[nvme] leaking bounce buffer after timeout (DMA may still be active)\n");
            } else {
                dma_free_coherent(bounce, alloc_bytes);
            }
            spinlock_release(&c->io_lock);
            return r;
        }
        if (!write) memcpy(bp, bounce, chunk_bytes);
        dma_free_coherent(bounce, alloc_bytes);

        bp  += chunk_bytes;
        cur += chunk;
        left -= chunk;
    }

    spinlock_release(&c->io_lock);
    return 0;
}

int nvme_read_sectors(nvme_namespace_t *ns, uint64_t lba, uint32_t count, void *buf) {
    return nvme_rw(ns, lba, count, buf, false);
}
int nvme_write_sectors(nvme_namespace_t *ns, uint64_t lba, uint32_t count, const void *buf) {
    return nvme_rw(ns, lba, count, (void *)buf, true);
}
int nvme_flush(nvme_namespace_t *ns) {
    if (!ns || !ns->active) return -EINVAL;
    nvme_controller_t *c = ns->ctrl;
    spinlock_acquire(&c->io_lock);
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_NVM_FLUSH;
    cmd.nsid   = ns->nsid;
    int r = nvme_submit_sync(c, false, &cmd, NULL);
    spinlock_release(&c->io_lock);
    return r;
}

const char *nvme_controller_model(nvme_namespace_t *ns) {
    if (!ns || !ns->ctrl) return "?";
    return ns->ctrl->model;
}
int nvme_namespace_count(void) { return g_ns_count; }
nvme_namespace_t *nvme_get_namespace(int idx) {
    if (idx < 0 || idx >= g_ns_count) return NULL;
    return g_namespaces[idx];
}

static int nvme_probe(pci_device_t *pd) {
    if (g_ctrl_count >= NVME_MAX_CONTROLLERS) return 0;
    if (pd->class_code != 0x01 || pd->subclass != 0x08 || pd->prog_if != 0x02) return 0;

    pci_bar_t *bar0 = &pd->bars[0];
    if (bar0->type != PCI_BAR_TYPE_MEM || bar0->base == 0) {
        serial_writestring("[nvme] BAR0 not present\n");
        return -EINVAL;
    }

    uint64_t bar_size = bar0->size ? bar0->size : 0x4000;
    bar_size = (bar_size + 0xFFFULL) & ~0xFFFULL;

    volatile uint8_t *bar0_virt =
        (volatile uint8_t *)mmio_map((uintptr_t)bar0->base, (size_t)bar_size);
    if (!bar0_virt) {
        serial_writestring("[nvme] mmio_map BAR0 failed\n");
        return -EIO;
    }

    uint16_t cmd_reg = pci_config_read16(pd->segment, pd->bus, pd->device, pd->function, PCI_COMMAND);
    cmd_reg |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DIS;
    pci_config_write16(pd->segment, pd->bus, pd->device, pd->function, PCI_COMMAND, cmd_reg);

    nvme_controller_t *c = &g_controllers[g_ctrl_count];
    memset(c, 0, sizeof(*c));
    c->pdev = pd;
    c->bar0 = bar0_virt;
    c->irq_vector = -1;

    uint64_t cap = reg_r64(c, NVME_CAP);
    c->mqes = (uint16_t)(cap & 0xFFFFu);
    uint8_t dstrd = (uint8_t)((cap >> 32) & 0xFu);
    c->doorbell_stride = 4u << dstrd;
    uint8_t mpsmin = (uint8_t)((cap >> 48) & 0xFu);
    uint32_t to    = (uint32_t)((cap >> 24) & 0xFFu);

    uint32_t vs = reg_r32(c, NVME_VS);
    serial_printf("[nvme] CAP=0x%llx VS=%u.%u.%u MQES=%u DSTRD=%u MPSMIN=%u TO=%u\n",
                  (unsigned long long)cap,
                  (unsigned)((vs >> 16) & 0xFFFFu),
                  (unsigned)((vs >> 8)  & 0xFFu),
                  (unsigned)( vs        & 0xFFu),
                  (unsigned)(c->mqes + 1), (unsigned)dstrd,
                  (unsigned)mpsmin, (unsigned)to);

    uint32_t cc = reg_r32(c, NVME_CC);
    cc &= ~CC_EN;
    reg_w32(c, NVME_CC, cc);

    uint64_t timeout_ms = (uint64_t)(to + 1) * 500;
    if (timeout_ms < 1000) timeout_ms = 1000;

    bool ready = false;
    for (uint64_t i = 0; i < timeout_ms; i++) {
        if ((reg_r32(c, NVME_CSTS) & CSTS_RDY) == 0) { ready = true; break; }
        hpet_sleep_ms(1);
    }
    if (!ready) {
        serial_writestring("[nvme] controller didn't clear RDY\n");
        return -EIO;
    }

    c->adm_depth = NVME_ADMIN_QUEUE_DEPTH;
    if (c->adm_depth > (uint16_t)(c->mqes + 1)) c->adm_depth = (uint16_t)(c->mqes + 1);
    c->adm_sq_virt = dma_alloc_coherent((size_t)c->adm_depth * 64, &c->adm_sq_phys);
    c->adm_cq_virt = dma_alloc_coherent((size_t)c->adm_depth * 16, &c->adm_cq_phys);
    if (!c->adm_sq_virt || !c->adm_cq_virt) {
        serial_writestring("[nvme] admin queue alloc failed\n");
        return -ENOMEM;
    }
    c->adm_sq_tail  = 0;
    c->adm_cq_head  = 0;
    c->adm_cq_phase = 1;

    uint32_t aqa = ((uint32_t)(c->adm_depth - 1) << 16) | (uint32_t)(c->adm_depth - 1);
    reg_w32(c, NVME_AQA, aqa);
    reg_w64(c, NVME_ASQ, c->adm_sq_phys);
    reg_w64(c, NVME_ACQ, c->adm_cq_phys);

    cc = CC_EN | CC_CSS_NVM | (0u << 7) | CC_AMS_RR | CC_SHN_NONE | CC_IOSQES_64 | CC_IOCQES_16;
    reg_w32(c, NVME_CC, cc);

    ready = false;
    for (uint64_t i = 0; i < timeout_ms; i++) {
        uint32_t csts = reg_r32(c, NVME_CSTS);
        if (csts & CSTS_CFS) {
            serial_writestring("[nvme] CSTS.CFS set, fatal controller fault\n");
            return -EIO;
        }
        if (csts & CSTS_RDY) { ready = true; break; }
        hpet_sleep_ms(1);
    }
    if (!ready) {
        serial_writestring("[nvme] controller didn't set RDY after enable\n");
        return -EIO;
    }
    serial_writestring("[nvme] controller ready\n");

    if (pd->cap_msix_off && pd->msix_table_size >= 1) {
        int vec = irq_alloc_vector();
        if (vec >= 0) {
            if (irq_request(vec, nvme_irq_handler, c, "nvme") == 0) {
                if (pci_enable_msix(pd, (uint8_t)vec, lapic_get_id()) == 0) {
                    c->irq_vector  = vec;
                    c->irq_enabled = true;
                    serial_printf("[nvme] MSI-X enabled on vector 0x%02x\n", vec);
                } else {
                    irq_free(vec);
                    irq_free_vector(vec);
                    serial_writestring("[nvme] pci_enable_msix failed, polling only\n");
                }
            } else {
                irq_free_vector(vec);
            }
        }
    } else if (pd->cap_msi_off) {
        int vec = irq_alloc_vector();
        if (vec >= 0) {
            if (irq_request(vec, nvme_irq_handler, c, "nvme") == 0) {
                if (pci_enable_msi(pd, (uint8_t)vec, lapic_get_id()) == 0) {
                    c->irq_vector  = vec;
                    c->irq_enabled = true;
                    serial_printf("[nvme] MSI enabled on vector 0x%02x\n", vec);
                } else {
                    irq_free(vec);
                    irq_free_vector(vec);
                }
            } else {
                irq_free_vector(vec);
            }
        }
    }

    if (nvme_identify_controller(c) < 0) {
        serial_writestring("[nvme] identify controller failed\n");
        return -EIO;
    }
    serial_printf("[nvme] model='%s' serial='%s' MDTS=%u\n",
                  c->model, c->serial_no, (unsigned)c->mdts);

    if (c->mdts == 0) {
        c->max_xfer_bytes = NVME_MAX_XFER_BYTES;
    } else {
        uint64_t mps_bytes = 4096ULL;
        uint64_t mx = mps_bytes << c->mdts;
        if (mx > (uint64_t)NVME_MAX_XFER_BYTES) mx = NVME_MAX_XFER_BYTES;
        c->max_xfer_bytes = (uint32_t)mx;
    }

    if (nvme_set_num_queues(c, 1) < 0) {
        serial_writestring("[nvme] set_features NUM_QUEUES failed\n");
    }

    c->io_depth = NVME_IO_QUEUE_DEPTH;
    if (c->io_depth > (uint16_t)(c->mqes + 1)) c->io_depth = (uint16_t)(c->mqes + 1);
    c->io_sq_virt = dma_alloc_coherent((size_t)c->io_depth * 64, &c->io_sq_phys);
    c->io_cq_virt = dma_alloc_coherent((size_t)c->io_depth * 16, &c->io_cq_phys);
    if (!c->io_sq_virt || !c->io_cq_virt) {
        serial_writestring("[nvme] io queue alloc failed\n");
        return -ENOMEM;
    }
    c->io_sq_tail  = 0;
    c->io_cq_head  = 0;
    c->io_cq_phase = 1;

    if (nvme_create_io_cq(c) < 0) {
        serial_writestring("[nvme] create IO CQ failed\n");
        return -EIO;
    }
    if (nvme_create_io_sq(c) < 0) {
        serial_writestring("[nvme] create IO SQ failed\n");
        return -EIO;
    }
    serial_writestring("[nvme] IO queue pair ready\n");

    uintptr_t list_phys;
    void *list_buf = dma_alloc_coherent(4096, &list_phys);
    if (!list_buf) return -ENOMEM;
    memset(list_buf, 0, 4096);
    if (nvme_get_active_ns_list(c, list_buf) < 0) {
        serial_writestring("[nvme] get_active_ns_list failed\n");
        dma_free_coherent(list_buf, 4096);
        return -EIO;
    }
    uint32_t *nsids = (uint32_t *)list_buf;

    uintptr_t ns_phys;
    void *ns_buf = dma_alloc_coherent(4096, &ns_phys);
    if (!ns_buf) {
        dma_free_coherent(list_buf, 4096);
        return -ENOMEM;
    }

    for (int i = 0; i < 1024 && c->ns_count < NVME_MAX_NAMESPACES; i++) {
        uint32_t nsid = nsids[i];
        if (nsid == 0) break;

        memset(ns_buf, 0, 4096);
        if (nvme_identify_namespace(c, nsid, ns_buf) < 0) {
            serial_printf("[nvme] identify nsid=%u failed\n", nsid);
            continue;
        }
        uint8_t *idns = (uint8_t *)ns_buf;
        uint64_t nsze;
        memcpy(&nsze, idns + 0, 8);
        uint8_t  flbas    = idns[26];
        uint8_t  cur_fmt  = flbas & 0xFu;
        uint8_t *lbaf     = idns + 128 + cur_fmt * 4;
        uint8_t  lbads    = lbaf[2];
        uint32_t lba_size = 1u << lbads;
        if (lba_size < 512 || lba_size > 4096) {
            serial_printf("[nvme] nsid=%u suspicious lba_size=%u, skipping\n", nsid, lba_size);
            continue;
        }

        nvme_namespace_t *ns = &c->namespaces[c->ns_count];
        ns->ctrl     = c;
        ns->nsid     = nsid;
        ns->active   = true;
        ns->sectors  = nsze;
        ns->lba_size = lba_size;
        snprintf(ns->name, sizeof(ns->name), "nvme%dn%d", g_ctrl_count, c->ns_count + 1);
        c->ns_count++;

        if (g_ns_count < (int)(sizeof(g_namespaces) / sizeof(g_namespaces[0]))) {
            g_namespaces[g_ns_count++] = ns;
        }

        serial_printf("[nvme] %s: %llu LBAs * %u B = %llu MB\n",
                      ns->name, (unsigned long long)nsze, lba_size,
                      (unsigned long long)((nsze * (uint64_t)lba_size) / (1024ULL * 1024ULL)));
    }

    dma_free_coherent(ns_buf,   4096);
    dma_free_coherent(list_buf, 4096);

    (void)mpsmin;
    g_ctrl_count++;
    return 0;
}

static const pci_driver_t g_nvme_driver = {
    .name           = "nvme",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = 0x01,
    .match_subclass = 0x08,
    .probe          = nvme_probe,
};

void nvme_init(void) {
    pci_register_driver(&g_nvme_driver);
}
