#include "../apps/cervus_user.h"

static const char *type_str(uint32_t t){
    switch(t){
        case 0: return "regular file";
        case 1: return "directory";
        case 2: return "char device";
        case 3: return "block device";
        case 4: return "symlink";
        case 5: return "pipe";
        default:return "unknown";
    }}

CERVUS_MAIN(stat_main) {
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