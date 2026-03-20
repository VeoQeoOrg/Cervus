#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static void print_u64_w(uint64_t v, int width){
    char t[22];int i=21;t[i]='\0';
    if(!v)t[--i]='0';
    else while(v){t[--i]='0'+v%10;v/=10;}
    int len=21-i;
    for(int p=len;p<width;p++)write(1," ",1);
    ws(t+i);}

static void count_file(int fd, uint64_t *lines, uint64_t *words, uint64_t *bytes){
    *lines=0; *words=0; *bytes=0;
    int in_word=0;
    char buf[512]; ssize_t n;
    while((n=read(fd,buf,sizeof(buf)))>0){
        *bytes+=n;
        for(ssize_t i=0;i<n;i++){
            char c=buf[i];
            if(c=='\n') (*lines)++;
            if(c==' '||c=='\t'||c=='\n'||c=='\r'){
                in_word=0;
            } else {
                if(!in_word){ (*words)++; in_word=1; }
            }
        }
    }
}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc<2){
        ws("Usage: wc <file> [file...]\n");
        exit(1);
    }

    uint64_t total_l=0, total_w=0, total_b=0;
    int multi=(argc>2);

    for(int i=1;i<argc;i++){
        char resolved[512];
        const char *path=argv[i];
        if(path[0]!='/'){
            const char *search[]={"/apps/","/bin/","/etc/","/",0};
            for(int s=0;search[s];s++){
                const char *pfx=search[s];
                int pl=0;while(pfx[pl])pl++;
                int nl=0;while(path[nl])nl++;
                if(pl+nl+1<(int)sizeof(resolved)){
                    int j=0;
                    for(int k=0;k<pl;k++)resolved[j++]=pfx[k];
                    for(int k=0;k<nl;k++)resolved[j++]=path[k];
                    resolved[j]='\0';
                    cervus_stat_t tmp;
                    if(stat(resolved,&tmp)==0){path=resolved;break;}
                }
            }
        }
        int fd=open(path,O_RDONLY,0);
        if(fd<0){
            ws("wc: cannot open: "); ws(path); wn();
            continue;
        }
        uint64_t l=0,w=0,b=0;
        count_file(fd,&l,&w,&b);
        close(fd);
        print_u64_w(l,7); write(1," ",1);
        print_u64_w(w,7); write(1," ",1);
        print_u64_w(b,7); write(1," ",1);
        ws(argv[i]); wn();
        total_l+=l; total_w+=w; total_b+=b;
    }
    if(multi){
        print_u64_w(total_l,7); write(1," ",1);
        print_u64_w(total_w,7); write(1," ",1);
        print_u64_w(total_b,7); ws(" total\n");
    }
    exit(0);
}