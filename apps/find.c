#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static int str_match(const char *name, const char *pat){
    while(*pat){
        if(*pat=='*'){
            pat++;
            if(!*pat) return 1;
            while(*name){
                if(str_match(name,pat)) return 1;
                name++;
            }
            return 0;
        } else if(*pat=='?'){
            if(!*name) return 0;
            name++; pat++;
        } else {
            if(*name!=*pat) return 0;
            name++; pat++;
        }
    }
    return *name=='\0';
}

static void path_join(char *out, size_t sz, const char *dir, const char *name){
    size_t dl=0; while(dir[dl])dl++;
    size_t nl=0; while(name[nl])nl++;
    if(dl+nl+2>=sz) return;
    size_t i=0;
    for(size_t j=0;j<dl;j++) out[i++]=dir[j];
    if(dl>0&&out[i-1]!='/') out[i++]='/';
    for(size_t j=0;j<nl;j++) out[i++]=name[j];
    out[i]='\0';
}

#define MAX_DEPTH 16

static void do_find(const char *dir, const char *pat, int depth){
    if(depth>MAX_DEPTH) return;

    int fd=open(dir,O_RDONLY|O_DIRECTORY,0);
    if(fd<0) return;

    cervus_dirent_t de;
    while(readdir(fd,&de)==0){
        if(de.d_name[0]=='.'&&
           (de.d_name[1]=='\0'||(de.d_name[1]=='.'&&de.d_name[2]=='\0')))
            continue;

        char path[512];
        path_join(path,sizeof(path),dir,de.d_name);

        if(!pat || str_match(de.d_name,pat)){
            ws(path);
            if(de.d_type==1) write(1,"/",1);
            wn();
        }

        if(de.d_type==1)
            do_find(path,pat,depth+1);
    }
    close(fd);
}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    const char *dir  = "/";
    const char *pat  = (void*)0;

    for(int i=1;i<argc;i++){
        if(argv[i][0]=='-'){
            if(argv[i][1]=='n'&&argv[i][2]=='a'&&argv[i][3]=='m'&&argv[i][4]=='e'){
                if(i+1<argc){ pat=argv[++i]; }
            }
        } else {
            dir=argv[i];
        }
    }

    ws(dir); wn();
    do_find(dir,pat,0);
    exit(0);
}