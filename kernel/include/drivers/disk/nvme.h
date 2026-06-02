#ifndef KERNEL_DRIVERS_NVME_H
#define KERNEL_DRIVERS_NVME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NVME_MAX_CONTROLLERS 4
#define NVME_MAX_NAMESPACES  8

typedef struct nvme_controller nvme_controller_t;

typedef struct {
    nvme_controller_t *ctrl;
    uint32_t  nsid;
    bool      active;
    uint64_t  sectors;
    uint32_t  lba_size;
    char      name[16];
} nvme_namespace_t;

void nvme_init(void);
int  nvme_namespace_count(void);
nvme_namespace_t *nvme_get_namespace(int index);

int nvme_read_sectors (nvme_namespace_t *ns, uint64_t lba, uint32_t count, void *buf);
int nvme_write_sectors(nvme_namespace_t *ns, uint64_t lba, uint32_t count, const void *buf);
int nvme_flush        (nvme_namespace_t *ns);

const char *nvme_controller_model(nvme_namespace_t *ns);

#endif
