#include "../../include/firmware/dmi.h"
#include "../../include/boot/boot_info.h"
#include "../../include/memory/dma.h"
#include "../../include/io/serial.h"
#include <stdio.h>
#include <string.h>

#define DMI_POOL_SIZE 2048

static char        g_pool[DMI_POOL_SIZE];
static size_t      g_pool_used;
static const char *g_ident[DMI_STRING_MAX];
static int         g_version;
static bool        g_present;

static const char *const g_names[DMI_STRING_MAX] = {
    [DMI_BIOS_VENDOR]         = "bios_vendor",
    [DMI_BIOS_VERSION]        = "bios_version",
    [DMI_BIOS_DATE]           = "bios_date",
    [DMI_BIOS_RELEASE]        = "bios_release",
    [DMI_EC_FIRMWARE_RELEASE] = "ec_firmware_release",
    [DMI_SYS_VENDOR]          = "sys_vendor",
    [DMI_PRODUCT_NAME]        = "product_name",
    [DMI_PRODUCT_VERSION]     = "product_version",
    [DMI_PRODUCT_SERIAL]      = "product_serial",
    [DMI_PRODUCT_UUID]        = "product_uuid",
    [DMI_PRODUCT_SKU]         = "product_sku",
    [DMI_PRODUCT_FAMILY]      = "product_family",
    [DMI_BOARD_VENDOR]        = "board_vendor",
    [DMI_BOARD_NAME]          = "board_name",
    [DMI_BOARD_VERSION]       = "board_version",
    [DMI_BOARD_SERIAL]        = "board_serial",
    [DMI_BOARD_ASSET_TAG]     = "board_asset_tag",
    [DMI_CHASSIS_VENDOR]      = "chassis_vendor",
    [DMI_CHASSIS_TYPE]        = "chassis_type",
    [DMI_CHASSIS_VERSION]     = "chassis_version",
    [DMI_CHASSIS_SERIAL]      = "chassis_serial",
    [DMI_CHASSIS_ASSET_TAG]   = "chassis_asset_tag",
};

static const char *pool_save(const char *s, size_t n)
{
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (g_pool_used + n + 1 > DMI_POOL_SIZE) return NULL;
    char *d = &g_pool[g_pool_used];
    memcpy(d, s, n);
    d[n] = '\0';
    g_pool_used += n + 1;
    return d;
}

static const char *dmi_string(const uint8_t *hdr, const uint8_t *limit, uint8_t idx, size_t *len)
{
    if (!idx) return NULL;
    const uint8_t *s = hdr + hdr[1];
    while (s < limit && --idx) {
        while (s < limit && *s) s++;
        s++;
    }
    if (s >= limit || !*s) return NULL;
    const uint8_t *e = s;
    while (e < limit && *e) e++;
    *len = (size_t)(e - s);
    return (const char *)s;
}

static void save_ident(const uint8_t *hdr, const uint8_t *limit, int field, uint8_t off)
{
    if (g_ident[field] || hdr[1] <= off) return;
    size_t n = 0;
    const char *s = dmi_string(hdr, limit, hdr[off], &n);
    if (s) g_ident[field] = pool_save(s, n);
}

static void save_uuid(const uint8_t *hdr, uint8_t off)
{
    if (g_ident[DMI_PRODUCT_UUID] || hdr[1] < off + 16) return;
    const uint8_t *d = hdr + off;
    bool all_ff = true, all_00 = true;
    for (int i = 0; i < 16; i++) {
        if (d[i] != 0xff) all_ff = false;
        if (d[i] != 0x00) all_00 = false;
    }
    if (all_ff || all_00) return;
    char buf[40];
    if (g_version >= 0x0206)
        snprintf(buf, sizeof(buf),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 d[3], d[2], d[1], d[0], d[5], d[4], d[7], d[6],
                 d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
    else
        snprintf(buf, sizeof(buf),
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
                 d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
    g_ident[DMI_PRODUCT_UUID] = pool_save(buf, strlen(buf));
}

static void save_release(const uint8_t *hdr, int field, uint8_t off)
{
    if (g_ident[field] || hdr[1] < off + 2) return;
    if (hdr[off] == 0xff && hdr[off + 1] == 0xff) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u", hdr[off], hdr[off + 1]);
    g_ident[field] = pool_save(buf, strlen(buf));
}

static void decode(const uint8_t *hdr, const uint8_t *limit)
{
    switch (hdr[0]) {
    case 0:
        save_ident(hdr, limit, DMI_BIOS_VENDOR, 4);
        save_ident(hdr, limit, DMI_BIOS_VERSION, 5);
        save_ident(hdr, limit, DMI_BIOS_DATE, 8);
        save_release(hdr, DMI_BIOS_RELEASE, 20);
        save_release(hdr, DMI_EC_FIRMWARE_RELEASE, 22);
        break;
    case 1:
        save_ident(hdr, limit, DMI_SYS_VENDOR, 4);
        save_ident(hdr, limit, DMI_PRODUCT_NAME, 5);
        save_ident(hdr, limit, DMI_PRODUCT_VERSION, 6);
        save_ident(hdr, limit, DMI_PRODUCT_SERIAL, 7);
        save_uuid(hdr, 8);
        save_ident(hdr, limit, DMI_PRODUCT_SKU, 25);
        save_ident(hdr, limit, DMI_PRODUCT_FAMILY, 26);
        break;
    case 2:
        save_ident(hdr, limit, DMI_BOARD_VENDOR, 4);
        save_ident(hdr, limit, DMI_BOARD_NAME, 5);
        save_ident(hdr, limit, DMI_BOARD_VERSION, 6);
        save_ident(hdr, limit, DMI_BOARD_SERIAL, 7);
        save_ident(hdr, limit, DMI_BOARD_ASSET_TAG, 8);
        break;
    case 3:
        save_ident(hdr, limit, DMI_CHASSIS_VENDOR, 4);
        if (!g_ident[DMI_CHASSIS_TYPE] && hdr[1] > 5) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", hdr[5] & 0x7f);
            g_ident[DMI_CHASSIS_TYPE] = pool_save(buf, strlen(buf));
        }
        save_ident(hdr, limit, DMI_CHASSIS_VERSION, 6);
        save_ident(hdr, limit, DMI_CHASSIS_SERIAL, 7);
        save_ident(hdr, limit, DMI_CHASSIS_ASSET_TAG, 8);
        break;
    }
}

static bool walk_table(uint64_t phys, uint32_t len, uint32_t count)
{
    if (!phys || len < 4 || len > 0x100000) return false;
    const uint8_t *base = (const uint8_t *)mmio_map((uintptr_t)phys, len);
    if (!base) return false;
    const uint8_t *p = base, *end = base + len;
    uint32_t n = 0;
    while (p + 4 <= end && (!count || n < count)) {
        uint8_t slen = p[1];
        if (slen < 4) break;
        if (p + slen > end) break;
        const uint8_t *s = p + slen;
        while (s + 1 < end && (s[0] || s[1])) s++;
        if (s + 1 >= end) break;
        decode(p, s + 2);
        n++;
        if (p[0] == 127) break;
        p = s + 2;
    }
    return n > 0;
}

static bool sum_ok(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]);
    return s == 0;
}

static bool try_entry(const uint8_t *e)
{
    if (!memcmp(e, "_SM3_", 5)) {
        uint8_t len = e[6];
        if (len < 0x18 || len > 0x20 || !sum_ok(e, len)) return false;
        g_version = (e[7] << 8) | e[8];
        uint32_t max;
        uint64_t addr;
        memcpy(&max, e + 0x0C, 4);
        memcpy(&addr, e + 0x10, 8);
        return walk_table(addr, max, 0);
    }
    if (!memcmp(e, "_SM_", 4)) {
        uint8_t len = e[5];
        if (len < 0x1E || len > 0x20 || !sum_ok(e, len)) return false;
        if (memcmp(e + 0x10, "_DMI_", 5) || !sum_ok(e + 0x10, 0x0F)) return false;
        g_version = (e[6] << 8) | e[7];
        if (g_version == 0x021F || g_version == 0x0221) g_version = 0x0203;
        if (g_version == 0x0233) g_version = 0x0206;
        uint16_t tlen, num;
        uint32_t addr;
        memcpy(&tlen, e + 0x16, 2);
        memcpy(&addr, e + 0x18, 4);
        memcpy(&num, e + 0x1C, 2);
        return walk_table(addr, tlen, num);
    }
    if (!memcmp(e, "_DMI_", 5)) {
        if (!sum_ok(e, 0x0F)) return false;
        g_version = ((e[14] & 0xF0) << 4) | (e[14] & 0x0F);
        uint16_t tlen, num;
        uint32_t addr;
        memcpy(&tlen, e + 6, 2);
        memcpy(&addr, e + 8, 4);
        memcpy(&num, e + 12, 2);
        return walk_table(addr, tlen, num);
    }
    return false;
}

static const uint8_t g_smbios3_guid[16] = {
    0x44, 0x15, 0xfd, 0xf2, 0x94, 0x97, 0x2c, 0x4a,
    0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 };
static const uint8_t g_smbios_guid[16] = {
    0x31, 0x2d, 0x9d, 0xeb, 0x88, 0x2d, 0xd3, 0x11,
    0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d };

static uint64_t efi_find_table(uint64_t systab, const uint8_t *guid)
{
    const uint8_t *st = (const uint8_t *)mmio_map((uintptr_t)systab, 120);
    if (!st || memcmp(st, "IBI SYST", 8)) return 0;
    uint64_t n, tab;
    memcpy(&n, st + 104, 8);
    memcpy(&tab, st + 112, 8);
    if (!tab || !n || n > 256) return 0;
    const uint8_t *ct = (const uint8_t *)mmio_map((uintptr_t)tab, (size_t)n * 24);
    if (!ct) return 0;
    for (uint64_t i = 0; i < n; i++) {
        if (!memcmp(ct + i * 24, guid, 16)) {
            uint64_t p;
            memcpy(&p, ct + i * 24 + 16, 8);
            return p;
        }
    }
    return 0;
}

static bool try_phys_entry(uint64_t phys)
{
    if (!phys) return false;
    const uint8_t *e = (const uint8_t *)mmio_map((uintptr_t)phys, 0x20);
    return e && try_entry(e);
}

static bool scan_legacy(void)
{
    const uint8_t *rom = (const uint8_t *)mmio_map(0xF0000, 0x10000);
    if (!rom) return false;
    for (uint32_t off = 0; off + 0x20 <= 0x10000; off += 16)
        if (!memcmp(rom + off, "_SM3_", 5) && try_entry(rom + off)) return true;
    for (uint32_t off = 0; off + 0x20 <= 0x10000; off += 16)
        if ((!memcmp(rom + off, "_SM_", 4) || !memcmp(rom + off, "_DMI_", 5)) && try_entry(rom + off))
            return true;
    return false;
}

static void print_ident(void)
{
    const char *v = g_ident[DMI_SYS_VENDOR];
    const char *p = g_ident[DMI_PRODUCT_NAME];
    const char *b = g_ident[DMI_BOARD_NAME];
    const char *bv = g_ident[DMI_BIOS_VERSION];
    const char *bd = g_ident[DMI_BIOS_DATE];
    printf("smbios%d.%d: %s %s%s%s, bios %s %s\n",
           g_version >> 8, g_version & 0xff,
           v ? v : "?", p ? p : "?", b ? "/" : "", b ? b : "",
           bv ? bv : "?", bd ? bd : "");
    serial_printf("smbios%d.%d: %s %s%s%s, bios %s %s\n",
                  g_version >> 8, g_version & 0xff,
                  v ? v : "?", p ? p : "?", b ? "/" : "", b ? b : "",
                  bv ? bv : "?", bd ? bd : "");
    for (int i = 1; i < DMI_STRING_MAX; i++)
        if (g_ident[i] && i != DMI_PRODUCT_SERIAL && i != DMI_BOARD_SERIAL &&
            i != DMI_CHASSIS_SERIAL && i != DMI_PRODUCT_UUID)
            LOG_I("[dmi] %s: %s\n", g_names[i], g_ident[i]);
}

void dmi_init(void)
{
    const boot_info_t *bi = boot_info();
    bool ok = false;
    if (bi->smbios_entry) ok = try_entry((const uint8_t *)(uintptr_t)bi->smbios_entry);
    if (!ok && bi->efi_systab) {
        ok = try_phys_entry(efi_find_table(bi->efi_systab, g_smbios3_guid));
        if (!ok) ok = try_phys_entry(efi_find_table(bi->efi_systab, g_smbios_guid));
    }
    if (!ok) ok = scan_legacy();
    g_present = ok;
    if (!ok) {
        LOG_W("[dmi] no SMBIOS tables found\n");
        return;
    }
    print_ident();
}

bool dmi_available(void) { return g_present; }

int dmi_version(void) { return g_version; }

const char *dmi_get_system_info(int field)
{
    if (field <= DMI_NONE || field >= DMI_STRING_MAX) return NULL;
    return g_ident[field];
}

const char *dmi_field_name(int field)
{
    if (field <= DMI_NONE || field >= DMI_STRING_MAX) return NULL;
    return g_names[field];
}
