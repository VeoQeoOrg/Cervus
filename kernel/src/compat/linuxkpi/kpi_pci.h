#ifndef CERVUS_KPI_PCI_H
#define CERVUS_KPI_PCI_H

#include "kpi.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/firmware/dmi.h"

#define pci_driver kpi_pci_driver
typedef unsigned long kernel_ulong_t;

#define PCI_ANY_ID (~0U)
#define PCI_VENDOR_ID_REALTEK 0x10ec
#define PCI_VENDOR_ID_INTEL   0x8086
#define PCI_DEVICE(v, d) .vendor = (v), .device = (d), .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
#define PCI_IRQ_INTX   1
#define PCI_IRQ_LEGACY 1
#define PCI_IRQ_MSI    2
#define PCI_IRQ_MSIX   4
#define IRQF_SHARED    0x80
#define KBUILD_MODNAME "rtw88"
#define PCI_D0    0
#define PCI_D3hot 3
#define PCI_EXP_LNKCTL         0x10
#define PCI_EXP_LNKCTL_ASPMC   0x0003
#define PCI_EXP_LNKCTL_ASPM_L0S 0x0001
#define PCI_EXP_LNKCTL_ASPM_L1  0x0002
#define PCI_EXP_LNKCTL_CLKREQ_EN 0x0100
#define PCI_EXP_DEVCTL2        0x28
#define PCI_EXP_DEVCTL2_LTR_EN 0x0400
#define PCI_EXP_DEVCTL2_COMP_TMOUT_DIS 0x0010
#define PCI_ERS_RESULT_DISCONNECT 1
#define PCI_ERS_RESULT_NEED_RESET 2
#define PCI_ERS_RESULT_RECOVERED  3
typedef int pci_ers_result_t;
typedef int pci_channel_state_t;

struct pci_device_id {
    u32 vendor, device, subvendor, subdevice;
    u32 class_, class_mask;
    unsigned long driver_data;
};

struct pci_dev {
    pci_device_t *pd;
    struct device dev;
    unsigned int irq;
    int irq_vector;
    u16 vendor, device;
    u16 subsystem_vendor, subsystem_device;
    u8 revision;
    char dev_name[16];
    char irq_name[24];
    irqreturn_t (*irq_handler)(int, void *);
    irqreturn_t (*irq_thread)(int, void *);
    void *irq_ctx;
    void *irq_task;
    wait_queue_head_t irq_wait;
    wait_queue_head_t irq_idle;
    volatile int irq_thread_pending;
    volatile int irq_thread_running;
    volatile int irq_hard_running;
    volatile int irq_polled;
    volatile int irq_disabled;
    volatile int irq_stop;
};

struct pci_error_handlers {
    pci_ers_result_t (*error_detected)(struct pci_dev *, pci_channel_state_t);
    pci_ers_result_t (*slot_reset)(struct pci_dev *);
    void (*resume)(struct pci_dev *);
};

struct dev_pm_ops { int dummy; };
#define SIMPLE_DEV_PM_OPS(name, s, r) const struct dev_pm_ops name = { 0 }
#define DEFINE_SIMPLE_DEV_PM_OPS(name, s, r) const struct dev_pm_ops name = { 0 }

struct pci_driver {
    const char *name;
    const struct pci_device_id *id_table;
    int  (*probe)(struct pci_dev *, const struct pci_device_id *);
    void (*remove)(struct pci_dev *);
    void (*shutdown)(struct pci_dev *);
    const struct pci_error_handlers *err_handler;
    struct { const struct dev_pm_ops *pm; } driver;
};

static inline void *pci_get_drvdata(struct pci_dev *p) { return p->dev.driver_data; }
static inline void  pci_set_drvdata(struct pci_dev *p, void *d) { p->dev.driver_data = d; }
static inline struct pci_dev *to_pci_dev(struct device *d) { return container_of(d, struct pci_dev, dev); }
int  kpi_pci_read_config_byte(struct pci_dev *p, int off, u8 *v);
int  kpi_pci_read_config_word(struct pci_dev *p, int off, u16 *v);
int  kpi_pci_read_config_dword(struct pci_dev *p, int off, u32 *v);
int  kpi_pci_write_config_byte(struct pci_dev *p, int off, u8 v);
int  kpi_pci_write_config_word(struct pci_dev *p, int off, u16 v);
int  kpi_pci_write_config_dword(struct pci_dev *p, int off, u32 v);
#define pci_read_config_byte(p, o, v)   kpi_pci_read_config_byte(p, o, v)
#define pci_read_config_word(p, o, v)   kpi_pci_read_config_word(p, o, v)
#define pci_read_config_dword(p, o, v)  kpi_pci_read_config_dword(p, o, v)
#define pci_write_config_byte(p, o, v)  kpi_pci_write_config_byte(p, o, v)
#define pci_write_config_word(p, o, v)  kpi_pci_write_config_word(p, o, v)
#define pci_write_config_dword(p, o, v) kpi_pci_write_config_dword(p, o, v)
int  kpi_pcie_capability_read_word(struct pci_dev *p, int pos, u16 *v);
int  kpi_pcie_capability_write_word(struct pci_dev *p, int pos, u16 v);
#define pcie_capability_read_word(p, pos, v)  kpi_pcie_capability_read_word(p, pos, v)
#define pcie_capability_write_word(p, pos, v) kpi_pcie_capability_write_word(p, pos, v)
static inline int pcie_capability_set_word(struct pci_dev *p, int pos, u16 set) {
    u16 v = 0; kpi_pcie_capability_read_word(p, pos, &v); return kpi_pcie_capability_write_word(p, pos, v | set);
}
static inline int pcie_capability_clear_word(struct pci_dev *p, int pos, u16 clr) {
    u16 v = 0; kpi_pcie_capability_read_word(p, pos, &v); return kpi_pcie_capability_write_word(p, pos, v & ~clr);
}
int  kpi_pci_enable_device(struct pci_dev *p);
void kpi_pci_set_master(struct pci_dev *p);
resource_size_t kpi_pci_resource_len(struct pci_dev *p, int bar);
resource_size_t kpi_pci_resource_start(struct pci_dev *p, int bar);
unsigned long kpi_pci_resource_flags(struct pci_dev *p, int bar);
void __iomem *kpi_pci_iomap(struct pci_dev *p, int bar, unsigned long len);
int  kpi_pci_alloc_irq_vectors(struct pci_dev *p, int min, int max, unsigned int flags);
void kpi_pci_free_irq_vectors(struct pci_dev *p);
int  kpi_request_threaded_irq(struct pci_dev *p, irqreturn_t (*h)(int, void *),
                              irqreturn_t (*t)(int, void *), void *ctx);
void kpi_free_irq(struct pci_dev *p);
void kpi_synchronize_irq(struct pci_dev *p);
void kpi_pci_start(void);
#define pci_enable_device(p)            kpi_pci_enable_device(p)
#define pci_disable_device(p)           do { } while (0)
#define pci_set_master(p)               kpi_pci_set_master(p)
#define pci_clear_master(p)             do { } while (0)
#define pci_request_regions(p, n)       0
#define pci_release_regions(p)          do { } while (0)
#define pci_resource_len(p, bar)        kpi_pci_resource_len(p, bar)
#define pci_resource_start(p, bar)      kpi_pci_resource_start(p, bar)
#define pci_resource_flags(p, bar)      kpi_pci_resource_flags(p, bar)
#define IORESOURCE_MEM                  0x200
#define IORESOURCE_IO                   0x100
#define pci_iomap(p, bar, len)          kpi_pci_iomap(p, bar, len)
#define pci_iounmap(p, a)               do { } while (0)
#define pci_upstream_bridge(p)          ((struct pci_dev *)0)
static inline int pci_set_power_state(struct pci_dev *p, int s) { (void)p; (void)s; return 0; }
static inline int pci_enable_wake(struct pci_dev *p, int s, bool e) { (void)p; (void)s; (void)e; return 0; }
#define pci_alloc_irq_vectors(p, mn, mx, f) kpi_pci_alloc_irq_vectors(p, mn, mx, f)
#define pci_free_irq_vectors(p)         kpi_pci_free_irq_vectors(p)
#define pci_irq_vector(p, n)            ((int)(p)->irq)
#define devm_request_threaded_irq(dev, irq, h, t, flags, name, ctx) \
    kpi_request_threaded_irq(to_pci_dev(dev), h, t, ctx)
#define request_threaded_irq(irq, h, t, flags, name, ctx) (-ENOSYS)
#define devm_free_irq(dev, irq, ctx)    kpi_free_irq(to_pci_dev(dev))
void kpi_synchronize_irq_num(unsigned int irq);
void kpi_disable_irq_num(unsigned int irq);
void kpi_enable_irq_num(unsigned int irq);
#define synchronize_irq(irq)            kpi_synchronize_irq_num(irq)
#define disable_irq(irq)                kpi_disable_irq_num(irq)
#define enable_irq(irq)                 kpi_enable_irq_num(irq)
struct dmi_strmatch { unsigned char slot:7; unsigned char exact_match:1; char substr[79]; };
struct dmi_system_id {
    int (*callback)(const struct dmi_system_id *);
    const char *ident;
    struct dmi_strmatch matches[4];
    void *driver_data;
};
#define DMI_MATCH(a, b)       { .slot = a, .substr = b }
#define DMI_EXACT_MATCH(a, b) { .slot = a, .substr = b, .exact_match = 1 }
int kpi_dmi_check_system(const struct dmi_system_id *list);
#define dmi_check_system(t) kpi_dmi_check_system(t)

int  kpi_pci_register_driver(struct pci_driver *drv);
#define pci_register_driver(d) kpi_pci_register_driver(d)

struct net_device { int dummy; char name[16]; };
struct napi_struct;
typedef int (*napi_poll_fn)(struct napi_struct *, int);
struct napi_struct {
    napi_poll_fn poll;
    struct net_device *dev;
    struct work_struct work;
    int state;
    volatile int enabled;
    int weight;
};
#define NAPI_POLL_WEIGHT 64
struct net_device *kpi_alloc_netdev_dummy(void);
void kpi_free_netdev(struct net_device *d);
void kpi_netif_napi_add(struct net_device *d, struct napi_struct *n, napi_poll_fn poll);
void kpi_napi_schedule(struct napi_struct *n);
bool kpi_napi_complete_done(struct napi_struct *n, int done);
void kpi_napi_enable(struct napi_struct *n);
void kpi_napi_disable(struct napi_struct *n);
#define alloc_netdev_dummy(x)           kpi_alloc_netdev_dummy()
#define init_dummy_netdev(d)            do { } while (0)
#define free_netdev(d)                  kpi_free_netdev(d)
#define netif_napi_add(d, n, poll)      kpi_netif_napi_add(d, n, poll)
#define netif_napi_del(n)               do { } while (0)
#define napi_schedule(n)                kpi_napi_schedule(n)
#define napi_complete_done(n, d)        kpi_napi_complete_done(n, d)
#define napi_complete(n)                kpi_napi_complete_done(n, 0)
#define napi_enable(n)                  kpi_napi_enable(n)
#define napi_disable(n)                 kpi_napi_disable(n)
void kpi_napi_synchronize(struct napi_struct *n);
#define napi_synchronize(n)             kpi_napi_synchronize(n)

#endif
