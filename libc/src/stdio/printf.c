#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

static void print_string(const char *str) {
    while (*str) {
        putchar(*str++);
    }
}

static void print_number(uint64_t num, int base, bool uppercase) {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[65]; 
    char *ptr = buffer + sizeof(buffer) - 1;
    *ptr = '\0';
    
    if (num == 0) {
        *--ptr = '0';
    } else {
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

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    int chars_written = 0;
    const char *ptr = format;
    
    while (*ptr) {
        if (*ptr != '%') {
            putchar(*ptr);
            chars_written++;
            ptr++;
            continue;
        }
        
        ptr++; 
        
        while (*ptr == '0' || *ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#') {
            ptr++;
        }
        
        while (*ptr >= '0' && *ptr <= '9') {
            ptr++;
        }
        
        if (*ptr == '.') {
            ptr++;
            while (*ptr >= '0' && *ptr <= '9') {
                ptr++;
            }
        }
        
        switch (*ptr) {
            case 'c': { 
                char c = (char)va_arg(args, int);
                putchar(c);
                chars_written++;
                break;
            }
            
            case 's': { 
                const char *str = va_arg(args, const char*);
                if (!str) {
                    str = "(null)";
                }
                while (*str) {
                    putchar(*str++);
                    chars_written++;
                }
                break;
            }
            
            case 'd': 
            case 'i': {
                int num = va_arg(args, int);
                print_signed_number(num, 10, false);
                if (num < 0) chars_written++;
                int temp = num == 0 ? 1 : 0;
                while (num != 0) {
                    num /= 10;
                    temp++;
                }
                chars_written += temp;
                break;
            }
            
            case 'u': { 
                unsigned int num = va_arg(args, unsigned int);
                print_number(num, 10, false);
                int temp = num == 0 ? 1 : 0;
                while (num != 0) {
                    num /= 10;
                    temp++;
                }
                chars_written += temp;
                break;
            }
            
            case 'x': { 
                unsigned int num = va_arg(args, unsigned int);
                print_number(num, 16, false);
                int temp = num == 0 ? 1 : 0;
                while (num != 0) {
                    num /= 16;
                    temp++;
                }
                chars_written += temp;
                break;
            }
            
            case 'X': { 
                unsigned int num = va_arg(args, unsigned int);
                print_number(num, 16, true);
                int temp = num == 0 ? 1 : 0;
                while (num != 0) {
                    num /= 16;
                    temp++;
                }
                chars_written += temp;
                break;
            }
            
            case 'p': { 
                void *ptr_val = va_arg(args, void*);
                print_string("0x");
                chars_written += 2;
                print_number((uintptr_t)ptr_val, 16, false);
                uintptr_t temp = (uintptr_t)ptr_val;
                int count = temp == 0 ? 1 : 0;
                while (temp != 0) {
                    temp /= 16;
                    count++;
                }
                chars_written += count;
                break;
            }

            case 'f':
            case 'F': {
                double num = va_arg(args, double);
                
                if (isinf(num)) {
                    const char *str = num < 0 ? "-inf" : "inf";
                    while (*str) {
                        putchar(*str++);
                        chars_written++;
                    }
                    break;
                }
                
                if (num < 0) {
                    putchar('-');
                    chars_written++;
                    num = -num;
                }
                
                int int_part = (int)num;
                char int_buffer[32];
                char *int_ptr = int_buffer + sizeof(int_buffer) - 1;
                *int_ptr = '\0';
                
                if (int_part == 0) {
                    *--int_ptr = '0';
                } else {
                    while (int_part > 0) {
                        *--int_ptr = '0' + (int_part % 10);
                        int_part /= 10;
                    }
                }
                
                while (*int_ptr) {
                    putchar(*int_ptr++);
                    chars_written++;
                }
                
                putchar('.');
                chars_written++;
                double frac = num - (int)num;
                
                for (int i = 0; i < 6; i++) {
                    frac *= 10;
                    int digit = (int)frac;
                    putchar('0' + digit);
                    chars_written++;
                    frac -= digit;
                }
                
                break;
            }
            
            case '%': { 
                putchar('%');
                chars_written++;
                break;
            }
            
            default: { 
                putchar('%');
                putchar(*ptr);
                chars_written += 2;
                break;
            }
        }
        
        ptr++; 
    }
    
    va_end(args);
    return chars_written;
}