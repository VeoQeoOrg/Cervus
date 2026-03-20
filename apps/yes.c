#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    const char *msg = (argc>=2) ? argv[1] : "y";

    for(;;){
        ws(msg);
        write(1,"\n",1);
    }
}