#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define MAX_DOUBLE_DIGITS 16
#define MAX_FLOAT_BUFFER 512

static void print_string(const char *str) {
    while (*str) putchar(*str++);
}

static void print_number(uint64_t num, int base, bool uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[65];
    char *ptr = buffer + sizeof(buffer) - 1;
    *ptr = '\0';

    if (num == 0) *--ptr = '0';
    else {
        while (num > 0) {
            *--ptr = digits[num % base];
            num /= base;
        }
    }
    print_string(ptr);
}

static void print_signed_number(int64_t num, int base, bool uppercase) {
    if (num < 0) {
        putchar('-');
        print_number(-num, base, uppercase);
    } else {
        print_number(num, base, uppercase);
    }
}

static void print_llu_number(uint64_t num, int base, bool uppercase) {
    print_number(num, base, uppercase);
}

static void print_signed_ll_number(int64_t num, int base, bool uppercase) {
    print_signed_number(num, base, uppercase);
}

static uint64_t pow10_u64(int n) {
    uint64_t r = 1;
    while (n-- > 0) r *= 10;
    return r;
}

static int double_to_string(double value, char *buffer, int precision) {
    if (isnan(value)) { strcpy(buffer, "nan"); return 3; }
    if (isinf(value)) { if (value < 0) strcpy(buffer, "-inf"); else strcpy(buffer, "inf"); return value < 0 ? 4 : 3; }

    if (precision < 0) precision = 6;
    if (precision > MAX_DOUBLE_DIGITS) precision = MAX_DOUBLE_DIGITS;

    bool negative = (value < 0);
    double abs_val = negative ? -value : value;

    uint64_t mult = pow10_u64(precision);
    double scaled = abs_val * mult + 0.5;
    uint64_t int_scaled = (uint64_t)scaled;

    uint64_t int_part = int_scaled / mult;
    uint64_t frac_part = int_scaled % mult;

    char *ptr = buffer;
    if (negative) *ptr++ = '-';

    char int_buf[32];
    char *p = int_buf + sizeof(int_buf) - 1;
    *p = '\0';
    if (int_part == 0) *--p = '0';
    else {
        uint64_t n = int_part;
        while (n) { *--p = '0' + (n % 10); n /= 10; }
    }
    strcpy(ptr, p);
    ptr += strlen(p);

    if (precision > 0) {
        *ptr++ = '.';
        char frac_buf[32];
        char *f = frac_buf + sizeof(frac_buf) - 1;
        *f = '\0';
        if (frac_part == 0) {
            for (int i = 0; i < precision; i++) *--f = '0';
        } else {
            uint64_t n = frac_part;
            int count = 0;
            while (n) { *--f = '0' + (n % 10); n /= 10; count++; }
            while (count < precision) { *--f = '0'; count++; }
        }
        strcpy(ptr, f);
        ptr += strlen(f);
    }
    *ptr = '\0';
    return ptr - buffer;
}

static int double_to_scientific(double value, char *buffer, int precision, bool uppercase) {
    if (value == 0.0) {
        if (precision < 0) precision = 6;
        buffer[0] = '0'; buffer[1] = '.';
        for (int i = 0; i < precision; i++) buffer[2+i] = '0';
        buffer[2+precision] = uppercase ? 'E' : 'e';
        buffer[3+precision] = '+'; buffer[4+precision] = '0'; buffer[5+precision] = '0'; buffer[6+precision] = '\0';
        return 6+precision;
    }

    if (isnan(value)) { strcpy(buffer, "nan"); return 3; }
    if (isinf(value)) { if (value < 0) strcpy(buffer, "-inf"); else strcpy(buffer, "inf"); return value < 0 ? 4 : 3; }

    if (precision < 0) precision = 6;
    if (precision > MAX_DOUBLE_DIGITS) precision = MAX_DOUBLE_DIGITS;

    double abs_val = value < 0 ? -value : value;
    int exp = 0;

    if (abs_val >= 10.0) { while (abs_val >= 10.0) { abs_val /= 10.0; exp++; } }
    else if (abs_val < 1.0 && abs_val > 0.0) { while (abs_val < 1.0) { abs_val *= 10.0; exp--; } }

    if (value < 0) abs_val = -abs_val;

    int len = double_to_string(abs_val, buffer, precision);

    char e_char = uppercase ? 'E' : 'e';
    buffer[len++] = e_char;
    if (exp >= 0) buffer[len++] = '+';
    else { buffer[len++] = '-'; exp = -exp; }
    if (exp < 10) { buffer[len++] = '0'; buffer[len++] = '0'+exp; }
    else { buffer[len++] = '0'+(exp/10); buffer[len++] = '0'+(exp%10); }

    buffer[len] = '\0';
    return len;
}

static int double_to_general(double value, char *buffer, int precision, bool uppercase) {
    double abs_val = value < 0 ? -value : value;
    if (abs_val == 0.0) { strcpy(buffer, "0"); return 1; }
    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1;

    bool use_sci = (abs_val >= 1e6 || (abs_val < 1e-4 && abs_val > 0));
    if (use_sci) return double_to_scientific(value, buffer, precision-1, uppercase);

    int digits_before = 0;
    double temp = abs_val;
    while (temp >= 1.0) { temp /= 10.0; digits_before++; }
    if (digits_before == 0) digits_before = 1;

    int dec_places = precision - digits_before;
    if (dec_places < 0) dec_places = 0;

    return double_to_string(value, buffer, dec_places);
}

static int double_to_hex(double value, char *buffer, int precision, bool uppercase) {
    char temp[MAX_FLOAT_BUFFER];
    int len = double_to_scientific(value, temp, precision, uppercase);
    buffer[0] = '0'; buffer[1] = 'x';
    strcpy(buffer+2, temp);
    return len+2;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int chars_written = 0;
    const char *ptr = format;

    while (*ptr) {
        if (*ptr != '%') { putchar(*ptr++); chars_written++; continue; }

        const char *percent_start = ptr++;
        while (*ptr == '0' || *ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#') ptr++;

        int width = 0;
        while (*ptr >= '0' && *ptr <= '9') { width = width*10 + (*ptr-'0'); ptr++; }

        int precision = -1;
        if (*ptr == '.') { ptr++; precision = 0; while (*ptr >= '0' && *ptr <= '9') { precision = precision*10 + (*ptr-'0'); ptr++; } }

        bool has_ll = false, is_size_t = false;
        if (*ptr == 'z') { is_size_t = true; ptr++; }
        else if (*ptr == 'l' && ptr[1]=='l') { has_ll = true; ptr+=2; }
        else if (*ptr=='L'||*ptr=='l'||*ptr=='h'||*ptr=='j'||*ptr=='t') ptr++;

        char buffer[MAX_FLOAT_BUFFER]; int buffer_len = 0;

        switch (*ptr) {
            case 'c': putchar((char)va_arg(args,int)); chars_written++; break;
            case 's': { const char *s=va_arg(args,const char*); if(!s)s="(null)"; while(*s){putchar(*s++); chars_written++;} } break;
            case 'd': case 'i': { int64_t num = has_ll ? va_arg(args,int64_t) : va_arg(args,int); print_signed_ll_number(num,10,false); int64_t n = num<0?-num:num; int temp = n==0?1:0; while(n){n/=10;temp++;} if(num<0) temp++; chars_written+=temp; } break;
            case 'u': { uint64_t num = has_ll ? va_arg(args,uint64_t) : va_arg(args,unsigned int); print_llu_number(num,10,false); uint64_t n=num; int temp=n==0?1:0; while(n){n/=10; temp++;} chars_written+=temp; } break;
            case 'x': case 'X': { bool upper=*ptr=='X'; uint64_t num=has_ll?va_arg(args,uint64_t):va_arg(args,unsigned int); print_llu_number(num,16,upper); uint64_t n=num; int temp=n==0?1:0; while(n){n>>=4; temp++;} chars_written+=temp; } break;
            case 'o': { uint64_t num=has_ll?va_arg(args,uint64_t):va_arg(args,unsigned int); print_llu_number(num,8,false); uint64_t n=num; int temp=n==0?1:0; while(n){n>>=3; temp++;} chars_written+=temp; } break;
            case 'f': case 'F': { double val=va_arg(args,double); buffer_len=double_to_string(val,buffer,precision); print_string(buffer); chars_written+=buffer_len; } break;
            case 'e': case 'E': { double val=va_arg(args,double); buffer_len=double_to_scientific(val,buffer,precision,*ptr=='E'); print_string(buffer); chars_written+=buffer_len; } break;
            case 'g': case 'G': { double val=va_arg(args,double); buffer_len=double_to_general(val,buffer,precision,*ptr=='G'); print_string(buffer); chars_written+=buffer_len; } break;
            case 'a': case 'A': { double val=va_arg(args,double); buffer_len=double_to_hex(val,buffer,precision,*ptr=='A'); print_string(buffer); chars_written+=buffer_len; } break;
            case 'p': { uintptr_t num=(uintptr_t)va_arg(args,void*); print_string("0x"); print_llu_number(num,16,false); uintptr_t n=num; int temp=n==0?1:0; while(n){n>>=4; temp++;} chars_written+=temp+2; } break;
            case 'n': { int *cnt=va_arg(args,int*); *cnt=chars_written; } break;
            case '%': putchar('%'); chars_written++; break;
            default: for(const char *p=percent_start;p<=ptr;p++){putchar(*p); chars_written++;} break;
        }
        ptr++;
    }

    va_end(args);
    return chars_written;
}
