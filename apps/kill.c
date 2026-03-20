#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static void print_u64(uint64_t v){
    if(!v){write(1,"0",1);return;}
    char t[22];int i=21;t[i]='\0';
    while(v){t[--i]='0'+v%10;v/=10;}ws(t+i);}

static int64_t parse_int(const char *s){
    int64_t v=0; int neg=0;
    if(*s=='-'){neg=1;s++;}
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return neg?-v:v;}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc<2){
        ws("Usage: kill <pid>\n");
        exit(1);
    }

    int64_t pid=parse_int(argv[1]);
    if(pid<=0){
        ws("kill: invalid pid\n");
        exit(1);
    }

    int r=task_kill((pid_t)pid);
    if(r<0){
        ws("kill: failed to kill pid ");
        print_u64((uint64_t)pid); wn();
        exit(1);
    }
    exit(0);
}