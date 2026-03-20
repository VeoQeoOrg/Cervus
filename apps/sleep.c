#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}

static uint64_t parse_uint(const char *s){
    uint64_t v=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return v;}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc<2){
        ws("Usage: sleep <seconds>\n");
        exit(1);
    }

    uint64_t secs=parse_uint(argv[1]);
    nanosleep_simple(secs * 1000000000ULL);
    exit(0);
}