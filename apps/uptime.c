#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static void print_u64(uint64_t v){
    if(!v){write(1,"0",1);return;}
    char t[22];int i=21;t[i]='\0';
    while(v){t[--i]='0'+v%10;v/=10;}
    ws(t+i);}

static void print_pad2(uint64_t v){
    if(v<10) write(1,"0",1);
    print_u64(v);}

void _start_main(uint64_t *sp){
    (void)sp;

    uint64_t ns = uptime_ns();
    uint64_t total_s  = ns / 1000000000ULL;
    uint64_t ms       = (ns / 1000000ULL) % 1000ULL;
    uint64_t secs     = total_s % 60;
    uint64_t mins     = (total_s / 60) % 60;
    uint64_t hours    = (total_s / 3600) % 24;
    uint64_t days     = total_s / 86400;

    ws("  Uptime: ");
    if(days>0){ print_u64(days); ws(" day"); if(days!=1)ws("s"); ws(", "); }
    print_pad2(hours); ws(":"); print_pad2(mins); ws(":"); print_pad2(secs);
    ws("  ("); print_u64(total_s); ws("s  ");
    print_u64(ms); ws("ms)\n");

    exit(0);
}