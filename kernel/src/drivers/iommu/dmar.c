#include "../../../include/drivers/iommu/dmar.h"
#include "../../../include/acpi/acpi.h"
#include "../../../include/memory/dma.h"
#include "../../../include/io/serial.h"
#include "../../../include/apic/apic.h"
#include <string.h>

#define DMAR_VER_REG   0x00
#define DMAR_CAP_REG   0x08
#define DMAR_ECAP_REG  0x10
#define DMAR_GCMD_REG  0x18
#define DMAR_GSTS_REG  0x1C
#define DMAR_PMEN_REG  0x64

#define GSTS_TES   (1u << 31)
#define GSTS_QIES  (1u << 26)
#define GSTS_IRES  (1u << 25)
#define GSTS_CFIS  (1u << 23)
#define GCMD_PERSISTENT (GSTS_TES | GSTS_QIES | GSTS_IRES | GSTS_CFIS)

#define PMEN_EPM   (1u << 31)
#define PMEN_PRS   (1u << 0)

#define DMAR_TYPE_DRHD 0
#define DMAR_MAX_UNITS 8

typedef struct {
    acpi_sdt_header_t header;
    uint8_t  host_address_width;
    uint8_t  flags;
    uint8_t  reserved[10];
} __attribute__((packed)) acpi_dmar_t;

typedef struct {
    uint16_t type;
    uint16_t length;
} __attribute__((packed)) dmar_struct_t;

typedef struct {
    uint16_t type;
    uint16_t length;
    uint8_t  flags;
    uint8_t  reserved;
    uint16_t segment;
    uint64_t base;
} __attribute__((packed)) dmar_drhd_t;

static int g_units;

static inline uint32_t rd32(volatile uint8_t *r, uint32_t off) { return *(volatile uint32_t *)(r + off); }
static inline void wr32(volatile uint8_t *r, uint32_t off, uint32_t v) { *(volatile uint32_t *)(r + off) = v; }

static int wait_clear(volatile uint8_t *r, uint32_t off, uint32_t bits) {
    for (int i = 0; i < 1000; i++) {
        if ((rd32(r, off) & bits) == 0) return 0;
        hpet_sleep_ms(1);
    }
    return -1;
}

static void dmar_quiesce_unit(const dmar_drhd_t *d) {
    volatile uint8_t *r = (volatile uint8_t *)mmio_map((uintptr_t)d->base, 0x1000);
    if (!r) {
        LOG_W("[dmar] cannot map the unit at 0x%llx\n", (unsigned long long)d->base);
        return;
    }
    uint32_t ver = rd32(r, DMAR_VER_REG);
    uint32_t gsts = rd32(r, DMAR_GSTS_REG);
    uint32_t pmen = rd32(r, DMAR_PMEN_REG);
    LOG_I("[dmar] unit %d at 0x%llx segment %u: version %u.%u%s, status 0x%08x, pmen 0x%08x\n",
          g_units, (unsigned long long)d->base, d->segment, (ver >> 4) & 0xF, ver & 0xF,
          (d->flags & 1) ? ", covers all devices" : "", gsts, pmen);

    uint32_t gcmd = gsts & GCMD_PERSISTENT;
    if (gsts & GSTS_TES) {
        gcmd &= ~GSTS_TES;
        wr32(r, DMAR_GCMD_REG, gcmd);
        if (wait_clear(r, DMAR_GSTS_REG, GSTS_TES) == 0)
            LOG_W("[dmar] unit %d: the firmware left DMA translation on; turned it off\n", g_units);
        else
            LOG_W("[dmar] unit %d: DMA translation did not turn off\n", g_units);
    }
    if (gsts & GSTS_IRES) {
        gcmd &= ~GSTS_IRES;
        wr32(r, DMAR_GCMD_REG, gcmd);
        if (wait_clear(r, DMAR_GSTS_REG, GSTS_IRES) == 0)
            LOG_W("[dmar] unit %d: the firmware left interrupt remapping on; turned it off\n", g_units);
        else
            LOG_W("[dmar] unit %d: interrupt remapping did not turn off\n", g_units);
    }
    if (gsts & GSTS_QIES) {
        gcmd &= ~GSTS_QIES;
        wr32(r, DMAR_GCMD_REG, gcmd);
        (void)wait_clear(r, DMAR_GSTS_REG, GSTS_QIES);
    }
    if (pmen & PMEN_EPM) {
        wr32(r, DMAR_PMEN_REG, pmen & ~PMEN_EPM);
        if (wait_clear(r, DMAR_PMEN_REG, PMEN_PRS) == 0)
            LOG_W("[dmar] unit %d: the firmware protected memory from DMA; protection turned off\n", g_units);
        else
            LOG_W("[dmar] unit %d: DMA memory protection did not turn off\n", g_units);
    }
}

void dmar_init(void) {
    acpi_dmar_t *t = (acpi_dmar_t *)acpi_find_table("DMAR", 0);
    if (!t) return;
    LOG_I("[dmar] VT-d present: host address width %u, flags 0x%x\n",
          t->host_address_width + 1, t->flags);
    uint8_t *p = (uint8_t *)t + sizeof(*t);
    uint8_t *end = (uint8_t *)t + t->header.length;
    while (p + sizeof(dmar_struct_t) <= end && g_units < DMAR_MAX_UNITS) {
        dmar_struct_t *s = (dmar_struct_t *)p;
        if (s->length < sizeof(dmar_struct_t) || p + s->length > end) break;
        if (s->type == DMAR_TYPE_DRHD && s->length >= sizeof(dmar_drhd_t)) {
            dmar_quiesce_unit((const dmar_drhd_t *)s);
            g_units++;
        }
        p += s->length;
    }
}

int dmar_unit_count(void) { return g_units; }
