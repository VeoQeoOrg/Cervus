#include "../apps/cervus_user.h"

static double d_abs(double v){ return v < 0.0 ? -v : v; }

static double d_powi(double base, int64_t exp){
    if(exp < 0){ base = 1.0/base; exp = -exp; }
    double r = 1.0;
    while(exp-- > 0) r *= base;
    return r;
}

static double d_pow(double base, double exp){
    int64_t iexp = (int64_t)exp;
    if((double)iexp == exp) return d_powi(base, iexp);
    if(base <= 0.0){ ws("calc: non-integer power of non-positive base\n"); return 0.0; }
    double x = base;
    int n = 0;
    while(x > 1.5){ x /= 2.0; n++; }
    while(x < 0.75){ x *= 2.0; n--; }
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y*y, term = y, lnx = 0.0;
    for(int i = 0; i < 20; i++){
        lnx += term / (2*i + 1);
        term *= y2;
    }
    lnx = lnx*2.0 + (double)n * 0.6931471805599453;
    double t = exp * lnx;
    double et = 1.0, term2 = 1.0;
    for(int i = 1; i < 30; i++){
        term2 *= t / (double)i;
        et += term2;
        if(d_abs(term2) < 1e-15) break;
    }
    return et;
}

static void print_double(double v){
    if(v != v){ ws("NaN"); return; }
    if(v > 1e300 && v == v+1){ ws("Inf"); return; }
    if(v <-1e300 && v == v-1){ ws("-Inf"); return; }

    if(v < 0.0){ write(1,"-",1); v = -v; }

    int use_exp = (v != 0.0 && (v >= 1e15 || v < 1e-6));

    int exp_val = 0;
    if(use_exp && v != 0.0){
        while(v >= 10.0){ v /= 10.0; exp_val++; }
        while(v <  1.0 ){ v *= 10.0; exp_val--; }
    }

    const int PREC = 10;
    double scale = 1.0;
    for(int i=0;i<PREC;i++) scale *= 10.0;
    double rounded = (double)(int64_t)(v * scale + 0.5) / scale;
    if(!use_exp && rounded >= 1e15){ use_exp = 1; }

    int64_t int_part = (int64_t)rounded;
    double frac = rounded - (double)int_part;

    char ibuf[22]; int ii = 21; ibuf[ii] = '\0';
    int64_t tmp = int_part;
    if(!tmp){ ibuf[--ii] = '0'; }
    else while(tmp){ ibuf[--ii] = '0' + (int)(tmp % 10); tmp /= 10; }
    ws(ibuf + ii);

    char fbuf[12];
    int flen = 0;
    for(int i=0;i<PREC;i++){
        frac *= 10.0;
        int d = (int)frac;
        if(d > 9) d = 9;
        fbuf[flen++] = '0' + d;
        frac -= (double)d;
    }
    while(flen > 0 && fbuf[flen-1] == '0') flen--;

    if(flen > 0){
        write(1,".",1);
        write(1,fbuf,flen);
    }

    if(use_exp){
        write(1,"e",1);
        if(exp_val < 0){ write(1,"-",1); exp_val = -exp_val; }
        else write(1,"+",1);
        char eb[8]; int ei=7; eb[ei]='\0';
        if(!exp_val) eb[--ei]='0';
        else while(exp_val){ eb[--ei]='0'+exp_val%10; exp_val/=10; }
        ws(eb+ei);
    }
}

static const char *g_pos;
static int g_err;

static void skip_ws(void){while(*g_pos==' '||*g_pos=='\t')g_pos++;}

static double parse_expr(void);

static double parse_number(void){
    skip_ws();
    int neg = 0;
    if(*g_pos == '-'){ neg = 1; g_pos++; skip_ws(); }

    if((*g_pos < '0' || *g_pos > '9') && *g_pos != '.'){
        ws("calc: expected number\n");
        g_err = 1; return 0.0;
    }

    double v = 0.0;
    while(*g_pos >= '0' && *g_pos <= '9'){
        v = v * 10.0 + (*g_pos - '0');
        g_pos++;
    }
    if(*g_pos == '.'){
        g_pos++;
        double frac = 0.1;
        while(*g_pos >= '0' && *g_pos <= '9'){
            v += (*g_pos - '0') * frac;
            frac *= 0.1;
            g_pos++;
        }
    }
    if(*g_pos == 'e' || *g_pos == 'E'){
        g_pos++;
        int eneg = 0;
        if(*g_pos == '+'){ g_pos++; }
        else if(*g_pos == '-'){ eneg = 1; g_pos++; }
        int eexp = 0;
        while(*g_pos >= '0' && *g_pos <= '9'){
            eexp = eexp * 10 + (*g_pos - '0');
            g_pos++;
        }
        double mul = 1.0;
        for(int i = 0; i < eexp; i++) mul *= 10.0;
        v = eneg ? v / mul : v * mul;
    }
    return neg ? -v : v;
}

static double parse_primary(void){
    skip_ws();
    if(*g_pos == '('){
        g_pos++;
        double v = parse_expr();
        skip_ws();
        if(*g_pos == ')') g_pos++;
        else{ ws("calc: missing ')'\n"); g_err = 1; }
        return v;
    }
    return parse_number();
}

static double parse_power(void){
    double left = parse_primary();
    skip_ws();
    if(*g_pos == '^'){
        g_pos++;
        double right = parse_power();
        if(g_err) return 0.0;
        return d_pow(left, right);
    }
    return left;
}

static double parse_term(void){
    double left = parse_power();
    for(;;){
        skip_ws();
        char op = *g_pos;
        if(op != '*' && op != '/' && op != '%') break;
        g_pos++;
        double right = parse_power();
        if(g_err) return 0.0;
        if(op == '*') left *= right;
        else if(right == 0.0){ ws("calc: division by zero\n"); g_err = 1; return 0.0; }
        else if(op == '/') left /= right;
        else {
            int64_t a = (int64_t)left, b = (int64_t)right;
            if(b == 0){ ws("calc: division by zero\n"); g_err = 1; return 0.0; }
            left = (double)(a % b);
        }
    }
    return left;
}

static double parse_expr(void){
    double left = parse_term();
    for(;;){
        skip_ws();
        char op = *g_pos;
        if(op != '+' && op != '-') break;
        g_pos++;
        double right = parse_term();
        if(g_err) return 0.0;
        if(op == '+') left += right;
        else          left -= right;
    }
    return left;
}

static char g_expr[512];

static void build_expr(int argc, char **argv){
    int pos = 0;
    int first = 1;
    for(int i = 1; i < argc && pos < 510; i++){
        if(is_shell_flag(argv[i])) continue;
        if(!first && pos < 510) g_expr[pos++] = ' ';
        first = 0;
        for(int j = 0; argv[i][j] && pos < 510; j++)
            g_expr[pos++] = argv[i][j];
    }
    g_expr[pos] = '\0';
}

static void cur_left(int n){
    if(n<=0) return;
    char buf[16]; int i=0;
    buf[i++]='\x1b'; buf[i++]='[';
    if(n>9){buf[i++]='0'+n/10;}
    buf[i++]='0'+n%10;
    buf[i++]='D';
    write(1,buf,i);
}
static void cur_right(int n){
    if(n<=0) return;
    char buf[16]; int i=0;
    buf[i++]='\x1b'; buf[i++]='[';
    if(n>9){buf[i++]='0'+n/10;}
    buf[i++]='0'+n%10;
    buf[i++]='C';
    write(1,buf,i);
}

static int readline_edit(char *buf, int max){
    int len = 0;
    int pos = 0;
    buf[0] = '\0';

    for(;;){
        char c;
        if(read(0,&c,1) <= 0) return (len > 0) ? len : -1;

        if(c == '\n' || c == '\r'){
            buf[len] = '\0';
            write(1,"\n",1);
            return len;
        }

        if(c == '\b' || c == 0x7F){
            if(pos > 0){
                for(int i=pos-1; i<len-1; i++) buf[i]=buf[i+1];
                len--; pos--;
                buf[len]='\0';
                write(1,"\b",1);
                write(1, buf+pos, len-pos);
                write(1," ",1);
                cur_left(len-pos+1);
            }
            continue;
        }

        if(c == 3){
            cur_left(pos);
            for(int i=0;i<len;i++) write(1," ",1);
            cur_left(len);
            buf[0]='\0'; return 0;
        }

        if(c == 0x1b){
            char s;
            if(read(0,&s,1)<=0) continue;
            if(s != '[') continue;

            int param = 0;
            char code;
            for(;;){
                if(read(0,&code,1)<=0) break;
                if(code >= '0' && code <= '9'){ param=param*10+(code-'0'); continue; }
                break;
            }

            if(code == 'D'){
                if(pos > 0){ pos--; cur_left(1); }
            } else if(code == 'C'){
                if(pos < len){ cur_right(1); pos++; }
            } else if(code == 'H' || (code=='~' && param==1)){
                cur_left(pos); pos=0;
            } else if(code == 'F' || (code=='~' && param==4)){
                cur_right(len-pos); pos=len;
            } else if(code == '~' && param==3){
                if(pos < len){
                    for(int i=pos; i<len-1; i++) buf[i]=buf[i+1];
                    len--; buf[len]='\0';
                    write(1, buf+pos, len-pos);
                    write(1," ",1);
                    cur_left(len-pos+1);
                }
            }
            continue;
        }

        if((unsigned char)c >= 0x20 && len < max-1){
            for(int i=len; i>pos; i--) buf[i]=buf[i-1];
            buf[pos]=c; len++; buf[len]='\0';
            write(1, buf+pos, len-pos);
            cur_left(len-pos-1);
            pos++;
        }
    }
}

CERVUS_MAIN(calc_main) {

    int real_argc = 0;
    for(int i = 1; i < argc; i++)
        if(!is_shell_flag(argv[i])) real_argc++;

    if(real_argc >= 1){
        build_expr(argc, argv);
        g_pos = g_expr; g_err = 0;
        double result = parse_expr();
        if(!g_err){ ws("= "); print_double(result); wn(); }
        exit(g_err ? 1 : 0);
    }

    ws("  Cervus Calc — floating-point arithmetic\n");
    ws("  Operators: + - * / % ^ ( )\n");
    ws("  Supports decimals (3.14) and exponents (1e3)\n");
    ws("  Type 'exit' to quit.\n\n");

    char line[256];
    for(;;){
        ws("> ");
        int n = readline_edit(line, sizeof(line));
        if(n < 0) break;
        if(n == 0) continue;
        if(line[0]=='e' && line[1]=='x' && line[2]=='i' &&
           line[3]=='t' && line[4]=='\0') break;

        g_pos = line; g_err = 0;
        double result = parse_expr();
        if(!g_err){ ws("= "); print_double(result); wn(); }
    }
    exit(0);
}