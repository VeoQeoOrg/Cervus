#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static const char hex[]="0123456789abcdef";

static void print_hex8(uint8_t v){
    char b[2]; b[0]=hex[v>>4]; b[1]=hex[v&0xF];
    write(1,b,2);}

static void print_hex64(uint64_t v){
    char b[16];
    for(int i=15;i>=0;i--){b[i]=hex[v&0xF];v>>=4;}
    write(1,b,16);}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc<2){
        ws("Usage: hexdump <file>\n");
        exit(1);
    }

    char resolved[512];
    const char *path=argv[1];
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
        ws("hexdump: cannot open: "); ws(path); wn();
        exit(1);
    }

    uint8_t buf[16];
    uint64_t offset=0;
    ssize_t n;

    while((n=read(fd,buf,16))>0){
        print_hex64(offset);
        ws("  ");

        for(int i=0;i<8;i++){
            if(i<n){ print_hex8(buf[i]); write(1," ",1); }
            else    ws("   ");
        }
        write(1," ",1);
        for(int i=8;i<16;i++){
            if(i<n){ print_hex8(buf[i]); write(1," ",1); }
            else    ws("   ");
        }

        ws(" |");
        for(int i=0;i<n;i++){
            char c=(buf[i]>=0x20&&buf[i]<0x7F)?(char)buf[i]:'.';
            write(1,&c,1);
        }
        ws("|\n");

        offset+=n;
    }

    print_hex64(offset); wn();

    close(fd);
    exit(0);
}