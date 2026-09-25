#include "../../../include/sched/spinlock.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/net/netdev.h"

static spinlock_t g_ne2000_lock = SPINLOCK_INIT;

void ne2000_lock(void) { spinlock_acquire(&g_ne2000_lock); }
void ne2000_unlock(void) { spinlock_release(&g_ne2000_lock); }

static pci_driver_t g_ne2000_driver = {
    .name           = "ne2000",
    .match_vendor   = 0x10EC,
    .match_device   = 0x8029,
    .match_class    = -1,
    .match_subclass = -1,
};

void ne2000_register(int (*probe)(pci_device_t *dev))
{
    g_ne2000_driver.probe = probe;
    pci_register_driver(&g_ne2000_driver);
}

void ne2000_set_link(void *ndev, int up)
{
    ((netdev_t *)ndev)->link_up = up;
}
