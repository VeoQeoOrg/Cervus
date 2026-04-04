#include "../apps/cervus_user.h"

static void print_pad2(int v){
    if(v<10) write(1," ",1);
    print_u64(v);}

static const char *MNAME[12]={
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"};

static int is_leap(int y){
    return (y%4==0&&y%100!=0)||(y%400==0);}

static int MDAYS[12]={31,28,31,30,31,30,31,31,30,31,30,31};

static int first_dow(int y, int m){
    int64_t days=0;
    for(int yr=1970;yr<y;yr++) days+=is_leap(yr)?366:365;
    int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    md[1]=is_leap(y)?29:28;
    for(int mo=0;mo<m-1;mo++) days+=md[mo];
    return (int)((days+4)%7);}

static void print_month(int y, int m){
    const char *mn=MNAME[m-1];
    int mnlen=0; while(mn[mnlen])mnlen++;
    int pad=(20-mnlen-5)/2;
    for(int i=0;i<pad;i++) write(1," ",1);
    ws(mn); write(1," ",1); print_u64(y); wn();
    ws(" Su Mo Tu We Th Fr Sa\n");
    int mdays=MDAYS[m-1];
    if(m==2&&is_leap(y)) mdays=29;
    int dow=first_dow(y,m);
    for(int i=0;i<dow;i++) ws("   ");
    for(int d=1;d<=mdays;d++){
        write(1," ",1);
        print_pad2(d);
        dow++;
        if(dow==7){ wn(); dow=0; }
    }
    if(dow!=0) wn();}

static uint64_t parse_uint(const char *s){
    uint64_t v=0;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return v;}

CERVUS_MAIN(cal_main) {
    int year=2025, month=1;
    cervus_timespec_t ts;
    if(clock_gettime(0,&ts)==0 && ts.tv_sec>0){
        int64_t t=ts.tv_sec;
        int y=1970;
        int64_t days=t/86400;
        while(1){int dy=is_leap(y)?366:365;if(days<dy)break;days-=dy;y++;}
        int mo=0;
        int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
        md[1]=is_leap(y)?29:28;
        while(mo<12){if(days<md[mo])break;days-=md[mo];mo++;}
        year=y; month=mo+1;
    }
    if(argc==3){
        month=(int)parse_uint(argv[1]);
        year =(int)parse_uint(argv[2]);
    } else if(argc==2){
        year=(int)parse_uint(argv[1]);
        ws("\n");
        for(int i=0;i<15;i++) write(1," ",1);
        print_u64(year); wn(); wn();
        for(int m=1;m<=12;m++){
            print_month(year,m);
            wn();
        }
        exit(0);
    }
    if(month<1||month>12){ ws("cal: invalid month\n"); exit(1); }
    wn();
    print_month(year,month);
    exit(0);
}