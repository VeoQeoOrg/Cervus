#include "../apps/cervus_user.h"

static const char *logo[] = {
    "    L          ",
    "   'k.i ,      ",
    "    \";\"+U.,    ",
    "       \\_' -.  ",
    "      .f  ,_.;.",
    "      I ,f     ",
    "       '       ",
    NULL
};

static void print_pad2(uint64_t v){
    if(v<10) write(1,"0",1);
    print_u64(v);}

void uptime() {
    uint64_t ns = uptime_ns();
    uint64_t total_s  = ns / 1000000000ULL;
    uint64_t ms       = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs     = total_s % 60;
    uint64_t mins     = (total_s / 60) % 60;
    uint64_t hours    = (total_s / 3600) % 24;
    uint64_t days     = total_s / 86400;
    ws("uptime: ");
    if(days>0){ print_u64(days); ws(" day"); if(days!=1)ws("s"); ws(", "); }
    print_pad2(hours); ws(":"); print_pad2(mins); ws(":"); print_pad2(secs);
    ws("  ("); print_u64(total_s); ws("s  ");
    print_u64(ms); ws("ms)");
}

static void cpuid_leaf(uint32_t leaf,
                       uint32_t *a, uint32_t *b,
                       uint32_t *c, uint32_t *d) {
    asm volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(leaf), "2"(0));
}

void cpu() {
    uint32_t a, b, c, d;
    cpuid_leaf(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        char brand[49]; uint32_t *p = (uint32_t *)brand;
        cpuid_leaf(0x80000002, &p[0],  &p[1],  &p[2],  &p[3]);
        cpuid_leaf(0x80000003, &p[4],  &p[5],  &p[6],  &p[7]);
        cpuid_leaf(0x80000004, &p[8],  &p[9],  &p[10], &p[11]);
        brand[48] = '\0';
        char *br = brand; while (*br == ' ') br++;
        ws("cpu: "); ws(br);
    }
}

static void print_size(uint64_t bytes) {
    if (bytes >= 1024ULL*1024*1024) {
        uint64_t w = bytes / (1024ULL*1024*1024);
        uint64_t f = (bytes % (1024ULL*1024*1024)) * 100 / (1024ULL*1024*1024);
        print_u64(w); ws("."); if(f<10)ws("0"); print_u64(f); ws(" GiB");
    } else if (bytes >= 1024ULL*1024) {
        uint64_t w = bytes / (1024ULL*1024);
        uint64_t f = (bytes % (1024ULL*1024)) * 100 / (1024ULL*1024);
        print_u64(w); ws("."); if(f<10)ws("0"); print_u64(f); ws(" MiB");
    } else {
        print_u64(bytes / 1024); ws(" KiB");
    }
}

void mem() {
	cervus_meminfo_t mi;
    if (meminfo(&mi) == 0) {
		ws("mem: "); print_size(mi.used_bytes); ws(" / "); print_size(mi.total_bytes);
    }
}

CERVUS_MAIN(fetch_main) {
	wn();
    for (int i = 0; logo[i]; i++) {
		ws(" ");

        ws(logo[i]);
		ws("  ");

		switch (i) {
			case 1:
				ws("os: Cervus OS");
				break;
			case 2:
				uptime();
				break;
			case 3:
				cpu();
				break;
			case 4:
				ws(C_RESET "shell: Cervus Shell");
				break;
			case 5:
				mem();
				break;
		}

        wn();
    }
	wn();
}
