#include "../apps/cervus_user.h"

static int64_t parse_int(const char *s){
    int64_t v=0; int neg=0;
    if(*s=='-'){neg=1;s++;}
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return neg?-v:v;}

CERVUS_MAIN(kill_main) {
    if(argc<2){
        ws("Usage: kill <pid>\n");
        exit(1);
    }
    int64_t pid=parse_int(argv[1]);
    if(pid<=0){
        ws("kill: invalid pid\n");
        exit(1);
    }
    cervus_task_info_t info;
    if(task_info((pid_t)pid, &info) < 0){
        ws("kill: no process with pid ");
        print_u64((uint64_t)pid); wn();
        exit(1);
    }
    int is_system = (pid == 1) || (info.ppid == 0 && info.uid == 0);
    if(is_system){
        ws("kill: '");
        ws(info.name);
        ws("' (pid ");
        print_u64((uint64_t)pid);
        ws(") is a system process. Kill anyway? [y/N] ");
        char answer[4];
        int n = 0;
        while(n < 3){
            char c; if(read(0,&c,1)<=0) break;
            if(c=='\n'||c=='\r'){ write(1,"\n",1); break; }
            write(1,&c,1); answer[n++]=c;
        }
        answer[n]='\0';
        if(n==0 || (answer[0]!='y' && answer[0]!='Y')){
            ws("kill: aborted\n");
            exit(0);
        }
    }
    int r=task_kill((pid_t)pid);
    if(r<0){
        ws("kill: failed to kill pid ");
        print_u64((uint64_t)pid); wn();
        exit(1);
    }
    exit(0);
}