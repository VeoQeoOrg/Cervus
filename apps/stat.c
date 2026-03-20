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

static void print_hex(uint64_t v){
    static const char h[]="0123456789abcdef";
    if(!v){ws("0x0");return;}
    char t[19];t[0]='0';t[1]='x';int i=18;t[18]='\0';
    while(v){t[--i]=h[v&0xF];v>>=4;}
    ws(t+i);}

static const char *type_str(uint32_t t){
    switch(t){
        case 0: return "regular file";
        case 1: return "directory";
        case 2: return "char device";
        case 3: return "block device";
        case 4: return "symlink";
        case 5: return "pipe";
        default:return "unknown";
    }
}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc<2){
        ws("Usage: stat <file>\n");
        exit(1);
    }

    for(int i=1;i<argc;i++){
        cervus_stat_t st;
        char resolved[512];
        const char *path = argv[i];
        if(path[0]!='/'){
            const char *search[]={"/apps/","/bin/","/etc/","/",0};
            int found=0;
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
                    if(stat(resolved,&tmp)==0){path=resolved;found=1;break;}
                }
            }
            if(!found){ws("stat: not found: ");ws(argv[i]);wn();continue;}
        }
        if(stat(path,&st)<0){
            ws("stat: cannot stat: "); ws(path); wn();
            continue;
        }
        ws("  File:   "); ws(argv[i]); wn();
        ws("  Type:   "); ws(type_str(st.st_type)); wn();
        ws("  Inode:  "); print_hex(st.st_ino); wn();
        ws("  Size:   "); print_u64(st.st_size); ws(" bytes\n");
        ws("  Blocks: "); print_u64(st.st_blocks); wn();
        ws("  UID:    "); print_u64(st.st_uid); wn();
        ws("  GID:    "); print_u64(st.st_gid); wn();
        if(i+1<argc) wn();
    }
    exit(0);
}