#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define MAX_MBR_PARTS 4
#define MAX_GPT_PARTS 16
#define ALIGN_LBA     2048

typedef struct {
    int       used;
    uint8_t   bootable;
    uint8_t   type;
    uint64_t  first_lba;
    uint64_t  last_lba;
    char      name[36];
    uint8_t   gpt_type_guid[16];
} part_entry_t;

typedef struct {
    char       devname[32];
    uint64_t   total_sectors;
    int        is_gpt;
    int        nparts;
    int        max_parts;
    part_entry_t parts[MAX_GPT_PARTS];
    int        dirty;
} fdisk_ctx_t;

static const uint8_t GUID_EFI[16] =
    {0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
static const uint8_t GUID_LINUX[16] =
    {0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79, 0x3D,0x69,0xD8,0x47,0x7D,0xE4};
static const uint8_t GUID_SWAP[16] =
    {0x6D,0xFD,0x57,0x06, 0xAB,0xA4, 0xC4,0x43, 0x84,0xE5, 0x09,0x33,0xC8,0x4B,0x4F,0x4F};
static const uint8_t GUID_MSDATA[16] =
    {0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7};

static const char *mbr_type_name(uint8_t t) {
    switch (t) {
        case 0x00: return "empty";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0xEE: return "GPT prot";
        case 0xEF: return "EFI System";
        default:   return "?";
    }
}

static const char *gpt_guid_name(const uint8_t guid[16]) {
    if (memcmp(guid, GUID_EFI,    16) == 0) return "EFI System";
    if (memcmp(guid, GUID_LINUX,  16) == 0) return "Linux";
    if (memcmp(guid, GUID_SWAP,   16) == 0) return "Linux swap";
    if (memcmp(guid, GUID_MSDATA, 16) == 0) return "Microsoft data";
    return "Other";
}

static int find_disk(const char *name, cervus_disk_info_t *out) {
    for (int i = 0; i < 64; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        if (cervus_disk_info(i, &info) < 0) break;
        if (!info.present || info.is_partition) continue;
        if (strcmp(info.name, name) == 0) { *out = info; return 0; }
    }
    return -1;
}

static int load_mbr(fdisk_ctx_t *c) {
    uint8_t sec[512];
    if (cervus_disk_read_raw(c->devname, 0, 1, sec) < 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return 1;

    for (int i = 0; i < 4; i++) {
        uint8_t *e = sec + 0x1BE + i * 16;
        uint8_t type = e[4];
        uint32_t lba = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
        uint32_t cnt = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
        if (type == 0 || cnt == 0) continue;
        c->parts[i].used      = 1;
        c->parts[i].bootable  = (e[0] == 0x80);
        c->parts[i].type      = type;
        c->parts[i].first_lba = lba;
        c->parts[i].last_lba  = (uint64_t)lba + cnt - 1;
        if (type == 0xEE) c->is_gpt = 1;
    }
    if (c->is_gpt) return 2;
    c->max_parts = MAX_MBR_PARTS;
    c->nparts = 4;
    return 0;
}

static int load_gpt(fdisk_ctx_t *c) {
    uint8_t hdr[512];
    if (cervus_disk_read_raw(c->devname, 1, 1, hdr) < 0) return -1;
    if (memcmp(hdr, "EFI PART", 8) != 0) return -1;

    uint32_t ent_count;
    uint32_t ent_size;
    uint64_t arr_lba;
    memcpy(&arr_lba,   hdr + 72, 8);
    memcpy(&ent_count, hdr + 80, 4);
    memcpy(&ent_size,  hdr + 84, 4);

    if (ent_size != 128) return -1;

    uint32_t need_sectors = (ent_count * ent_size + 511) / 512;
    if (need_sectors > 64) need_sectors = 64;

    uint8_t *buf = malloc((size_t)need_sectors * 512);
    if (!buf) return -1;

    if (cervus_disk_read_raw(c->devname, arr_lba, need_sectors, buf) < 0) {
        free(buf);
        return -1;
    }

    memset(c->parts, 0, sizeof(c->parts));
    c->nparts = 0;
    c->max_parts = MAX_GPT_PARTS;

    uint32_t check = ent_count > MAX_GPT_PARTS ? MAX_GPT_PARTS : ent_count;
    for (uint32_t i = 0; i < check; i++) {
        uint8_t *e = buf + i * ent_size;
        int zero = 1;
        for (int k = 0; k < 16; k++) if (e[k]) { zero = 0; break; }
        if (zero) {
            c->parts[i].used = 0;
        } else {
            c->parts[i].used = 1;
            memcpy(c->parts[i].gpt_type_guid, e, 16);
            memcpy(&c->parts[i].first_lba, e + 32, 8);
            memcpy(&c->parts[i].last_lba,  e + 40, 8);
            for (int k = 0; k < 36; k++) {
                uint16_t u = ((uint16_t)e[56 + k * 2]) | ((uint16_t)e[56 + k * 2 + 1] << 8);
                if (!u) break;
                c->parts[i].name[k] = (char)(u & 0x7F);
            }
        }
        c->nparts++;
    }
    if (c->nparts < MAX_GPT_PARTS) c->nparts = MAX_GPT_PARTS;
    free(buf);
    return 0;
}

static void print_table(fdisk_ctx_t *c) {
    printf("\nDevice: /dev/%s   Total: %lu sectors (%lu MiB)   Label: %s\n",
           c->devname, (unsigned long)c->total_sectors,
           (unsigned long)((c->total_sectors * 512ULL) / (1024ULL * 1024ULL)),
           c->is_gpt ? "GPT" : "MBR");
    printf("# %-4s %-10s %-12s %-12s %-12s %s\n",
           "Slot", "Boot", "Start LBA", "End LBA", "Size MiB", c->is_gpt ? "Type" : "Type (hex)");
    for (int i = 0; i < c->nparts; i++) {
        part_entry_t *p = &c->parts[i];
        if (!p->used) {
            printf("  %-4d  -          -            -            -            (free)\n", i + 1);
            continue;
        }
        uint64_t sz = (p->last_lba - p->first_lba + 1) * 512ULL / (1024ULL * 1024ULL);
        if (c->is_gpt) {
            printf("  %-4d  -          %-12lu %-12lu %-12lu %s%s%s\n",
                   i + 1,
                   (unsigned long)p->first_lba,
                   (unsigned long)p->last_lba,
                   (unsigned long)sz,
                   gpt_guid_name(p->gpt_type_guid),
                   p->name[0] ? " '" : "",
                   p->name[0] ? p->name : "");
        } else {
            printf("  %-4d  %-10s %-12lu %-12lu %-12lu 0x%02X %s\n",
                   i + 1,
                   p->bootable ? "*" : "-",
                   (unsigned long)p->first_lba,
                   (unsigned long)p->last_lba,
                   (unsigned long)sz,
                   p->type,
                   mbr_type_name(p->type));
        }
    }
    if (c->dirty) printf("\n  (unsaved changes — type 'w' to write)\n");
}

static uint64_t find_free_start(fdisk_ctx_t *c) {
    uint64_t start = ALIGN_LBA;
    int progressed;
    do {
        progressed = 0;
        for (int i = 0; i < c->nparts; i++) {
            if (!c->parts[i].used) continue;
            if (start >= c->parts[i].first_lba && start <= c->parts[i].last_lba) {
                start = c->parts[i].last_lba + 1;
                if (start % ALIGN_LBA) start = ((start / ALIGN_LBA) + 1) * ALIGN_LBA;
                progressed = 1;
            }
        }
    } while (progressed);
    return start;
}

static uint64_t find_free_end(fdisk_ctx_t *c, uint64_t start) {
    uint64_t end = c->total_sectors - 1;
    if (c->is_gpt) end = c->total_sectors - 34;
    for (int i = 0; i < c->nparts; i++) {
        if (!c->parts[i].used) continue;
        if (c->parts[i].first_lba > start && c->parts[i].first_lba - 1 < end) {
            end = c->parts[i].first_lba - 1;
        }
    }
    return end;
}

static int read_line(const char *prompt, char *buf, size_t cap) {
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, cap, stdin)) return -1;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return 0;
}

static uint64_t parse_size(const char *s, uint64_t max_sectors) {
    if (!s || !*s) return max_sectors;
    if (strcmp(s, "rest") == 0 || strcmp(s, "*") == 0) return max_sectors;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s) return 0;
    if (strcmp(end, "MiB") == 0 || strcmp(end, "M") == 0) return (v * 1024ULL * 1024ULL) / 512ULL;
    if (strcmp(end, "GiB") == 0 || strcmp(end, "G") == 0) return (v * 1024ULL * 1024ULL * 1024ULL) / 512ULL;
    if (strcmp(end, "KiB") == 0 || strcmp(end, "K") == 0) return (v * 1024ULL) / 512ULL;
    return v;
}

static void cmd_new(fdisk_ctx_t *c) {
    int slot = -1;
    for (int i = 0; i < c->nparts; i++) if (!c->parts[i].used) { slot = i; break; }
    if (slot < 0) { fputs("  no free slots\n", stdout); return; }

    uint64_t start = find_free_start(c);
    uint64_t end_max = find_free_end(c, start);
    if (start >= end_max) { fputs("  no free space\n", stdout); return; }

    printf("  free range: LBA %lu .. %lu (%lu MiB available)\n",
           (unsigned long)start, (unsigned long)end_max,
           (unsigned long)((end_max - start + 1) * 512ULL / (1024ULL * 1024ULL)));

    char buf[64];
    if (read_line("  size (e.g. 64MiB, rest): ", buf, sizeof(buf)) < 0) return;
    uint64_t want = parse_size(buf, end_max - start + 1);
    if (want == 0) { fputs("  bad size\n", stdout); return; }
    if (want > end_max - start + 1) want = end_max - start + 1;

    if (c->is_gpt) {
        if (read_line("  type [efi/linux/swap/data]: ", buf, sizeof(buf)) < 0) return;
        const uint8_t *g = GUID_LINUX;
        if (strcasecmp(buf, "efi")  == 0) g = GUID_EFI;
        else if (strcasecmp(buf, "swap") == 0) g = GUID_SWAP;
        else if (strcasecmp(buf, "data") == 0) g = GUID_MSDATA;
        memcpy(c->parts[slot].gpt_type_guid, g, 16);

        if (read_line("  name (optional, <=36 chars): ", buf, sizeof(buf)) < 0) return;
        memset(c->parts[slot].name, 0, sizeof(c->parts[slot].name));
        strncpy(c->parts[slot].name, buf, sizeof(c->parts[slot].name) - 1);
    } else {
        if (read_line("  type (hex, e.g. 83): ", buf, sizeof(buf)) < 0) return;
        char *end = NULL;
        unsigned long v = strtoul(buf, &end, 16);
        if (end == buf || v > 0xFF) { fputs("  bad hex\n", stdout); return; }
        c->parts[slot].type = (uint8_t)v;
        c->parts[slot].bootable = (slot == 0) ? 1 : 0;
    }

    c->parts[slot].used      = 1;
    c->parts[slot].first_lba = start;
    c->parts[slot].last_lba  = start + want - 1;
    c->dirty = 1;
    printf("  added slot %d (LBA %lu..%lu)\n",
           slot + 1, (unsigned long)start, (unsigned long)(start + want - 1));
}

static void cmd_delete(fdisk_ctx_t *c) {
    char buf[16];
    if (read_line("  slot to delete: ", buf, sizeof(buf)) < 0) return;
    int idx = atoi(buf) - 1;
    if (idx < 0 || idx >= c->nparts) { fputs("  bad slot\n", stdout); return; }
    if (!c->parts[idx].used) { fputs("  already empty\n", stdout); return; }
    memset(&c->parts[idx], 0, sizeof(c->parts[idx]));
    c->dirty = 1;
    printf("  slot %d deleted\n", idx + 1);
}

static void cmd_switch_to_gpt(fdisk_ctx_t *c) {
    memset(c->parts, 0, sizeof(c->parts));
    c->nparts = MAX_GPT_PARTS;
    c->max_parts = MAX_GPT_PARTS;
    c->is_gpt = 1;
    c->dirty = 1;
    fputs("  switched to empty GPT\n", stdout);
}

static void cmd_switch_to_mbr(fdisk_ctx_t *c) {
    memset(c->parts, 0, sizeof(c->parts));
    c->nparts = MAX_MBR_PARTS;
    c->max_parts = MAX_MBR_PARTS;
    c->is_gpt = 0;
    c->dirty = 1;
    fputs("  switched to empty MBR\n", stdout);
}

static int cmd_write(fdisk_ctx_t *c) {
    char buf[16];
    if (read_line("  confirm WRITE? [yes/N]: ", buf, sizeof(buf)) < 0) return -1;
    if (strcmp(buf, "yes") != 0) { fputs("  aborted\n", stdout); return -1; }

    if (c->is_gpt) {
        struct cervus_gpt_entry_spec specs[MAX_GPT_PARTS];
        int n = 0;
        for (int i = 0; i < c->nparts; i++) {
            if (!c->parts[i].used) continue;
            memcpy(specs[n].type_guid, c->parts[i].gpt_type_guid, 16);
            specs[n].first_lba = c->parts[i].first_lba;
            specs[n].last_lba  = c->parts[i].last_lba;
            memset(specs[n].name, 0, sizeof(specs[n].name));
            strncpy(specs[n].name, c->parts[i].name, sizeof(specs[n].name) - 1);
            n++;
        }
        if (n == 0) { fputs("  no partitions to write\n", stdout); return -1; }
        int r = cervus_disk_partition_gpt(c->devname, specs, (uint64_t)n);
        if (r < 0) { fprintf(stderr, "  write failed: %d\n", r); return -1; }
    } else {
        cervus_mbr_part_t mbr[MAX_MBR_PARTS];
        memset(mbr, 0, sizeof(mbr));
        int real = 0;
        for (int i = 0; i < MAX_MBR_PARTS; i++) {
            if (!c->parts[i].used) continue;
            mbr[i].boot_flag    = c->parts[i].bootable ? 1 : 0;
            mbr[i].type         = c->parts[i].type;
            mbr[i].lba_start    = (uint32_t)c->parts[i].first_lba;
            mbr[i].sector_count = (uint32_t)(c->parts[i].last_lba - c->parts[i].first_lba + 1);
            real = i + 1;
        }
        if (real == 0) { fputs("  no partitions to write\n", stdout); return -1; }
        int r = cervus_disk_partition(c->devname, mbr, (uint64_t)real);
        if (r < 0) { fprintf(stderr, "  write failed: %d\n", r); return -1; }
    }

    c->dirty = 0;
    fputs("  written.\n", stdout);
    return 0;
}

static const char USAGE[] =
    "Usage: fdisk <device>\n"
    "Interactive MBR/GPT partition editor.\n"
    "Commands:\n"
    "  p   print current table\n"
    "  n   add new partition\n"
    "  d   delete partition\n"
    "  g   create empty GPT label\n"
    "  o   create empty MBR label\n"
    "  w   write changes to disk\n"
    "  q   quit without writing\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "fdisk")) return 0;

    const char *devname = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!devname) devname = argv[i];
    }
    if (!devname) { fputs(USAGE, stdout); return 1; }

    const char *short_name = devname;
    if (strncmp(short_name, "/dev/", 5) == 0) short_name += 5;

    cervus_disk_info_t info;
    if (find_disk(short_name, &info) < 0) {
        fprintf(stderr, "fdisk: device '%s' not found (whole disks only)\n", short_name);
        return 1;
    }

    fdisk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    strncpy(ctx.devname, short_name, sizeof(ctx.devname) - 1);
    ctx.total_sectors = info.sectors;
    ctx.max_parts = MAX_MBR_PARTS;
    ctx.nparts = MAX_MBR_PARTS;

    int mbr_r = load_mbr(&ctx);
    if (mbr_r == 2) {
        if (load_gpt(&ctx) < 0) {
            fputs("fdisk: protective MBR present but GPT header unreadable\n", stderr);
            return 1;
        }
    }

    print_table(&ctx);

    char line[64];
    for (;;) {
        if (read_line("\nfdisk> ", line, sizeof(line)) < 0) break;
        if (strlen(line) == 0) continue;
        switch (line[0]) {
            case 'p': print_table(&ctx); break;
            case 'n': cmd_new(&ctx); break;
            case 'd': cmd_delete(&ctx); break;
            case 'g': cmd_switch_to_gpt(&ctx); break;
            case 'o': cmd_switch_to_mbr(&ctx); break;
            case 'w':
                if (cmd_write(&ctx) == 0) return 0;
                break;
            case 'q':
                if (ctx.dirty) {
                    if (read_line("  unsaved changes — quit anyway? [yes/N]: ",
                                  line, sizeof(line)) < 0) return 0;
                    if (strcmp(line, "yes") != 0) break;
                }
                return 0;
            case '?':
            case 'h':
                fputs(USAGE, stdout);
                break;
            default:
                fputs("  unknown command (try 'h')\n", stdout);
        }
    }
    return 0;
}
