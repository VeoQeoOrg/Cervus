#include "../apps/cervus_user.h"

static void print_pad2(uint64_t v){
    if(v<10) write(1,"0",1);
    print_u64(v);}

static void print_pad4(uint64_t v){
    if(v<1000) write(1,"0",1);
    if(v<100)  write(1,"0",1);
    if(v<10)   write(1,"0",1);
    print_u64(v);}

static const int MDAYS[12]={31,28,31,30,31,30,31,31,30,31,30,31};
static const char *MNAME[12]={
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"};
static const char *WDAY[7]={
    "Thu","Fri","Sat","Sun","Mon","Tue","Wed"};

static int is_leap(int y){
    return (y%4==0&&y%100!=0)||(y%400==0);}

CERVUS_MAIN(date_main) {
    (void)argc; (void)argv;
    cervus_timespec_t ts;
    if(clock_gettime(0,&ts)<0 || ts.tv_sec==0){
        uint64_t up=uptime_ns()/1000000000ULL;
        ws("  System clock not available.\n");
        ws("  Uptime: "); print_u64(up); ws("s\n");
        exit(0);
    }
    int64_t t=ts.tv_sec;
    int wday=(int)((t/86400+4)%7);
    int64_t days=t/86400;
    int64_t rem =t%86400;
    if(rem<0){rem+=86400;days--;}
    int hour=(int)(rem/3600);
    int min =(int)((rem%3600)/60);
    int sec =(int)(rem%60);
    int year=1970;
    while(1){
        int dy=is_leap(year)?366:365;
        if(days<dy) break;
        days-=dy; year++;
    }
    int mon=0;
    while(mon<12){
        int dm=MDAYS[mon]+(mon==1&&is_leap(year)?1:0);
        if(days<dm) break;
        days-=dm; mon++;
    }
    int mday=(int)days+1;
    ws("  ");
    ws(WDAY[wday]); ws(" ");
    ws(MNAME[mon]); ws(" ");
    if(mday<10) write(1," ",1);
    print_u64(mday); ws(" ");
    print_pad2(hour); ws(":"); print_pad2(min); ws(":"); print_pad2(sec);
    ws(" UTC "); print_pad4(year); wn();
    exit(0);
}