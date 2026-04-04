#include "../apps/cervus_user.h"

static void print_padded(uint64_t v, int width){
    char t[22]; int i=21; t[i]='\0';
    if(!v){ t[--i]='0'; }
    else{ while(v){ t[--i]='0'+v%10; v/=10; } }
    int len=21-i;
    for(int p=len;p<width;p++) write(1," ",1);
    ws(t+i);}

static const char *state_str(uint32_t s){
    switch(s){
        case 0: return "RUNNING ";
        case 1: return "READY   ";
        case 2: return "BLOCKED ";
        case 3: return "ZOMBIE  ";
        default:return "UNKNOWN ";
    }}

CERVUS_MAIN(ps_main) {
    (void)argc; (void)argv;
    ws("  PID  PPID  UID  STATE    PRIO  NAME\n");
    ws("  ---  ----  ---  -------  ----  ----------------\n");
    uint32_t seen[512]; int nseen=0;
    for(pid_t pid = 0; pid < 512; pid++){
        cervus_task_info_t info;
        if(task_info(pid, &info) < 0) continue;
        int dup=0;
        for(int s=0;s<nseen;s++) if(seen[s]==info.pid){dup=1;break;}
        if(dup) continue;
        if(nseen<512) seen[nseen++]=info.pid;
        write(1,"  ",2);
        print_padded(info.pid,  4);
        write(1,"  ",2);
        print_padded(info.ppid, 4);
        write(1,"  ",2);
        print_padded(info.uid,  3);
        write(1,"  ",2);
        ws(state_str(info.state));
        write(1,"  ",2);
        print_padded(info.priority, 4);
        write(1,"  ",2);
        ws(info.name);
        wn();
    }
    exit(0);
}