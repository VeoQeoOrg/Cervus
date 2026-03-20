#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi\nand $-16,%%rsp\ncall _start_main\nud2\n":::"memory");
}

static void ws(const char *s){size_t n=0;while(s[n])n++;write(1,s,n);}
static void wn(void){write(1,"\n",1);}

static void print_i64(int64_t v){
    if(v<0){write(1,"-",1);v=-v;}
    if(!v){write(1,"0",1);return;}
    char t[22];int i=21;t[i]='\0';
    while(v){t[--i]='0'+v%10;v/=10;}
    ws(t+i);}

static const char *g_pos;
static int g_err;

static void skip_ws(void){while(*g_pos==' '||*g_pos=='\t')g_pos++;}

static int64_t parse_expr(void);

static int64_t parse_number(void){
    skip_ws();
    int neg=0;
    if(*g_pos=='-'){neg=1;g_pos++;}
    if(*g_pos<'0'||*g_pos>'9'){
        ws("calc: expected number\n");
        g_err=1; return 0;
    }
    int64_t v=0;
    while(*g_pos>='0'&&*g_pos<='9'){v=v*10+(*g_pos-'0');g_pos++;}
    return neg?-v:v;
}

static int64_t parse_primary(void){
    skip_ws();
    if(*g_pos=='('){
        g_pos++;
        int64_t v=parse_expr();
        skip_ws();
        if(*g_pos==')') g_pos++;
        else{ ws("calc: missing ')'\n"); g_err=1; }
        return v;
    }
    return parse_number();
}

static int64_t pow_i64(int64_t base, int64_t exp){
    if(exp<0) return 0;
    int64_t r=1;
    while(exp-->0) r*=base;
    return r;}

static int64_t parse_power(void){
    int64_t left=parse_primary();
    skip_ws();
    if(*g_pos=='^'){
        g_pos++;
        int64_t right=parse_power();
        return pow_i64(left,right);
    }
    return left;}

static int64_t parse_term(void){
    int64_t left=parse_power();
    for(;;){
        skip_ws();
        char op=*g_pos;
        if(op!='*'&&op!='/'&&op!='%') break;
        g_pos++;
        int64_t right=parse_power();
        if(g_err) return 0;
        if(op=='*') left*=right;
        else if(right==0){ ws("calc: division by zero\n"); g_err=1; return 0; }
        else if(op=='/') left/=right;
        else             left%=right;
    }
    return left;}

static int64_t parse_expr(void){
    int64_t left=parse_term();
    for(;;){
        skip_ws();
        char op=*g_pos;
        if(op!='+'&&op!='-') break;
        g_pos++;
        int64_t right=parse_term();
        if(g_err) return 0;
        if(op=='+') left+=right;
        else        left-=right;
    }
    return left;}

static char g_expr[512];

static void build_expr(int argc, char **argv){
    int pos=0;
    for(int i=1;i<argc&&pos<510;i++){
        if(i>1&&pos<510) g_expr[pos++]=' ';
        for(int j=0;argv[i][j]&&pos<510;j++)
            g_expr[pos++]=argv[i][j];
    }
    g_expr[pos]='\0';
}

static int readline_simple(char *buf, int max){
    int n=0;
    while(n<max-1){
        char c; if(read(0,&c,1)<=0) return -1;
        if(c=='\n'||c=='\r'){buf[n]='\0';write(1,"\n",1);return n;}
        if(c=='\b'||c==0x7F){
            if(n>0){n--;write(1,"\b \b",3);}
        } else {
            buf[n++]=c; write(1,&c,1);
        }
    }
    buf[n]='\0'; return n;
}

void _start_main(uint64_t *sp){
    (void)sp;
    int argc=(int)sp[0];
    char **argv=(char**)(sp+1);

    if(argc>=2){
        build_expr(argc,argv);
        g_pos=g_expr; g_err=0;
        int64_t result=parse_expr();
        if(!g_err){ ws("= "); print_i64(result); wn(); }
        exit(g_err?1:0);
    }

    ws("  Cervus Calc — integer arithmetic\n");
    ws("  Operators: + - * / % ^ ( )\n");
    ws("  Type 'exit' to quit.\n\n");

    char line[256];
    for(;;){
        ws("> ");
        int n=readline_simple(line,sizeof(line));
        if(n<0) break;
        if(n==0) continue;

        if(line[0]=='e'&&line[1]=='x'&&line[2]=='i'&&
           line[3]=='t'&&line[4]=='\0') break;

        g_pos=line; g_err=0;
        int64_t result=parse_expr();
        if(!g_err){ ws("= "); print_i64(result); wn(); }
    }
    exit(0);
}