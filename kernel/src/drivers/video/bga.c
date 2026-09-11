#include "../../../include/drivers/video/bga.h"
#include "../../../include/drivers/pci.h"
#include "../../../include/graphics/fb/fb.h"
#include "../../../include/io/ports.h"
#include "../../../include/io/serial.h"
#include "../../../include/memory/dma.h"
#include <string.h>

#define VBE_IOPORT_INDEX  0x01CE
#define VBE_IOPORT_DATA   0x01CF

#define VBE_IDX_ID          0x0
#define VBE_IDX_XRES        0x1
#define VBE_IDX_YRES        0x2
#define VBE_IDX_BPP         0x3
#define VBE_IDX_ENABLE      0x4
#define VBE_IDX_VIRT_WIDTH  0x6
#define VBE_IDX_VIRT_HEIGHT 0x7
#define VBE_IDX_X_OFFSET    0x8
#define VBE_IDX_Y_OFFSET    0x9

#define VBE_DISABLED      0x00
#define VBE_ENABLED       0x01
#define VBE_LFB_ENABLED   0x40

#define VBE_ID0           0xB0C0
#define VBE_ID5           0xB0C5

extern fb_info_t *global_framebuffer;

static int       g_present;
static uint64_t  g_vram_bytes;
static volatile void *g_vram;

static const bga_mode_t g_modes[] = {
    {  640,  480 }, {  800,  600 }, { 1024,  600 }, { 1024,  768 },
    { 1152,  864 }, { 1280,  720 }, { 1280,  768 }, { 1280,  800 },
    { 1280, 1024 }, { 1366,  768 }, { 1440,  900 }, { 1600,  900 },
    { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
};
#define N_MODES ((int)(sizeof g_modes / sizeof g_modes[0]))

static void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_IOPORT_INDEX, index);
    outw(VBE_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_IOPORT_INDEX, index);
    return inw(VBE_IOPORT_DATA);
}

int bga_present(void) { return g_present; }

int bga_list_modes(bga_mode_t *out, int max) {
    if (!g_present || !out) return 0;
    int n = 0;
    for (int i = 0; i < N_MODES && n < max; i++) {
        uint64_t need = (uint64_t)g_modes[i].width * g_modes[i].height * 4ull;
        if (g_vram_bytes && need > g_vram_bytes) continue;
        out[n++] = g_modes[i];
    }
    return n;
}

int bga_set_mode(uint32_t width, uint32_t height) {
    if (!g_present || !global_framebuffer) return -1;
    if (width < 640 || height < 400 || width > 1920 || height > 1200) return -1;
    if (width & 7) return -1;

    uint64_t need = (uint64_t)width * height * 4ull;
    if (g_vram_bytes && need > g_vram_bytes) return -1;

    bga_write(VBE_IDX_ENABLE, VBE_DISABLED);
    bga_write(VBE_IDX_XRES, (uint16_t)width);
    bga_write(VBE_IDX_YRES, (uint16_t)height);
    bga_write(VBE_IDX_VIRT_WIDTH, (uint16_t)width);
    bga_write(VBE_IDX_VIRT_HEIGHT, (uint16_t)height);
    bga_write(VBE_IDX_BPP, 32);
    bga_write(VBE_IDX_X_OFFSET, 0);
    bga_write(VBE_IDX_Y_OFFSET, 0);
    bga_write(VBE_IDX_ENABLE, VBE_ENABLED | VBE_LFB_ENABLED);

    if (bga_read(VBE_IDX_XRES) != (uint16_t)width ||
        bga_read(VBE_IDX_YRES) != (uint16_t)height) {
        serial_printf("[bga] adapter refused %ux%u\n", width, height);
        return -1;
    }

    uint16_t vw = bga_read(VBE_IDX_VIRT_WIDTH);
    if (vw < width) vw = (uint16_t)width;

    if (g_vram) global_framebuffer->address = (void *)g_vram;
    global_framebuffer->width  = width;
    global_framebuffer->height = height;
    global_framebuffer->pitch  = (uint64_t)vw * 4ull;
    global_framebuffer->bpp    = 32;
    return 0;
}

static int bga_probe(pci_device_t *dev) {
    if (g_present) return -1;
    if (dev->bars[0].type != PCI_BAR_TYPE_MEM || !dev->bars[0].base) return -1;

    uint16_t id = bga_read(VBE_IDX_ID);
    if (id < VBE_ID0 || id > VBE_ID5) return -1;

    uint16_t cmd = pci_config_read16(dev->segment, dev->bus, dev->device,
                                     dev->function, PCI_COMMAND);
    pci_config_write16(dev->segment, dev->bus, dev->device, dev->function,
                       PCI_COMMAND, (uint16_t)(cmd | PCI_COMMAND_MEMORY));

    g_vram_bytes = dev->bars[0].size;
    g_vram = mmio_map(dev->bars[0].base, dev->bars[0].size);
    if (!g_vram) {
        serial_printf("[bga] could not map %lluMB of video memory\n",
                      (unsigned long long)(g_vram_bytes / (1024 * 1024)));
        return -1;
    }
    g_present = 1;

    serial_printf("[bga] %04x:%04x id=0x%04x vram=%lluMB, runtime modes available\n",
                  dev->vendor_id, dev->device_id, id,
                  (unsigned long long)(g_vram_bytes / (1024 * 1024)));
    return 0;
}

static const uint32_t g_bga_ids[] = {
    0x12341111u,
    0x80EEBEEFu,
    0x1AF41050u,
};

static const pci_driver_t g_bga_driver = {
    .name           = "bga",
    .match_vendor   = -1,
    .match_device   = -1,
    .match_class    = -1,
    .match_subclass = -1,
    .match_ids      = g_bga_ids,
    .match_id_count = (int)(sizeof g_bga_ids / sizeof g_bga_ids[0]),
    .probe          = bga_probe,
};

void bga_init(void) {
    pci_register_driver(&g_bga_driver);
}
