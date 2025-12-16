#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define MAX_DOUBLE_DIGITS 16
#define MAX_FLOAT_BUFFER 512

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

static void print_llu_number(uint64_t num, int base, bool uppercase) {
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

static void print_signed_ll_number(int64_t num, int base, bool uppercase) {
    if (num < 0) {
        putchar('-');
        print_llu_number(-num, base, uppercase);
    } else {
        print_llu_number(num, base, uppercase);
    }
}

static int double_to_string(double value, char *buffer, int precision) {
    if (isinf(value)) {
        if (value < 0) {
            strcpy(buffer, "-inf");
            return 4;
        } else {
            strcpy(buffer, "inf");
            return 3;
        }
    }
    
    if (isnan(value)) {
        strcpy(buffer, "nan");
        return 3;
    }
    
    if (precision < 0) precision = 6;
    if (precision > MAX_DOUBLE_DIGITS) precision = MAX_DOUBLE_DIGITS;
    
    int is_negative = (value < 0);
    double abs_value = fabs(value);
    
    double multiplier = pow10(precision);
    double scaled = abs_value * multiplier;
    
    uint64_t int_scaled = (uint64_t)(scaled + 0.5);
    
    if (int_scaled >= (uint64_t)(multiplier * 10.0)) {
        abs_value += 1.0;
        scaled = abs_value * multiplier;
        int_scaled = (uint64_t)(scaled + 0.5);
    }
    
    uint64_t int_part = int_scaled / (uint64_t)multiplier;
    uint64_t frac_part = int_scaled % (uint64_t)multiplier;
    
    char int_buffer[65];
    char *ptr = int_buffer + sizeof(int_buffer) - 1;
    *ptr = '\0';
    
    if (int_part == 0) {
        *--ptr = '0';
    } else {
        uint64_t n = int_part;
        while (n > 0) {
            *--ptr = '0' + (n % 10);
            n /= 10;
        }
    }
    
    char *buf_ptr = buffer;
    
    if (is_negative) {
        *buf_ptr++ = '-';
    }
    
    strcpy(buf_ptr, ptr);
    buf_ptr += strlen(ptr);
    
    if (precision > 0) {
        *buf_ptr++ = '.';
        
        uint64_t temp = frac_part;
        int digits = 0;
        while (temp > 0) {
            temp /= 10;
            digits++;
        }
        
        for (int i = digits; i < precision; i++) {
            *buf_ptr++ = '0';
        }
        
        if (frac_part > 0) {
            char frac_buffer[20];
            char *frac_ptr = frac_buffer + sizeof(frac_buffer) - 1;
            *frac_ptr = '\0';
            
            uint64_t n = frac_part;
            while (n > 0) {
                *--frac_ptr = '0' + (n % 10);
                n /= 10;
            }
            
            strcpy(buf_ptr, frac_ptr);
            buf_ptr += strlen(frac_ptr);
        }
    }
    
    *buf_ptr = '\0';
    return buf_ptr - buffer;
}

static int double_to_scientific(double value, char *buffer, int precision, bool uppercase) {
    if (value == 0.0) {
        if (precision < 0) precision = 6;
        
        buffer[0] = '0';
        buffer[1] = '.';
        
        int pos = 2;
        for (int i = 0; i < precision; i++) {
            buffer[pos++] = '0';
        }
        
        buffer[pos++] = uppercase ? 'E' : 'e';
        buffer[pos++] = '+';
        buffer[pos++] = '0';
        buffer[pos++] = '0';
        buffer[pos] = '\0';
        
        return pos;
    }
    
    if (isinf(value)) {
        if (value < 0) {
            strcpy(buffer, "-inf");
            return 4;
        } else {
            strcpy(buffer, "inf");
            return 3;
        }
    }
    
    if (isnan(value)) {
        strcpy(buffer, "nan");
        return 3;
    }
    
    if (precision < 0) precision = 6;
    if (precision > MAX_DOUBLE_DIGITS) precision = MAX_DOUBLE_DIGITS;
    
    double abs_value = fabs(value);
    int exponent = 0;
    
    if (abs_value >= 10.0) {
        while (abs_value >= 10.0) {
            abs_value /= 10.0;
            exponent++;
        }
    } else if (abs_value < 1.0 && abs_value > 0.0) {
        while (abs_value < 1.0) {
            abs_value *= 10.0;
            exponent--;
        }
    }
    
    if (value < 0) abs_value = -abs_value;
    
    int len = double_to_string(abs_value, buffer, precision);
    
    char exp_char = uppercase ? 'E' : 'e';
    buffer[len++] = exp_char;
    
    if (exponent >= 0) {
        buffer[len++] = '+';
    } else {
        buffer[len++] = '-';
        exponent = -exponent;
    }
    
    if (exponent < 10) {
        buffer[len++] = '0';
        buffer[len++] = '0' + exponent;
    } else if (exponent < 100) {
        buffer[len++] = '0' + (exponent / 10);
        buffer[len++] = '0' + (exponent % 10);
    } else {
        char exp_str[10];
        char *exp_ptr = exp_str + sizeof(exp_str) - 1;
        *exp_ptr = '\0';
        
        do {
            *--exp_ptr = '0' + (exponent % 10);
            exponent /= 10;
        } while (exponent > 0);
        
        while (*exp_ptr) {
            buffer[len++] = *exp_ptr++;
        }
    }
    
    buffer[len] = '\0';
    return len;
}

static int double_to_general(double value, char *buffer, int precision, bool uppercase) {
    double abs_value = fabs(value);
    
    if (abs_value == 0.0) {
        strcpy(buffer, "0");
        return 1;
    }
    
    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1; 
    
    bool use_scientific = false;
    
    if (abs_value >= 1e6) use_scientific = true;
    if (abs_value < 1e-4 && abs_value > 0) use_scientific = true;
    
    int significant_digits = precision;
    
    if (use_scientific) {
        return double_to_scientific(value, buffer, significant_digits - 1, uppercase);
    } else {
        double temp = abs_value;
        int digits_before_decimal = 0;
        
        while (temp >= 1.0) {
            temp /= 10.0;
            digits_before_decimal++;
        }
        
        if (digits_before_decimal == 0) {
            digits_before_decimal = 1; 
        }
        
        int decimal_places = significant_digits - digits_before_decimal;
        if (decimal_places < 0) decimal_places = 0;
        
        return double_to_string(value, buffer, decimal_places);
    }
}

static int double_to_hex(double value, char *buffer, int precision, bool uppercase) {
    if (isinf(value)) {
        if (value < 0) {
            strcpy(buffer, "-inf");
            return 4;
        } else {
            strcpy(buffer, "inf");
            return 3;
        }
    }
    
    if (isnan(value)) {
        strcpy(buffer, "nan");
        return 3;
    }
    
    char temp[MAX_FLOAT_BUFFER];
    int len = double_to_scientific(value, temp, precision, uppercase);
    
    buffer[0] = '0';
    buffer[1] = 'x';
    strcpy(buffer + 2, temp);
    
    return len + 2;
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
        
        const char *percent_start = ptr;
        ptr++; 
        
        while (*ptr == '0' || *ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#') {
            ptr++;
        }
        
        int width = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            width = width * 10 + (*ptr - '0');
            ptr++;
        }
        
        int precision = -1;
        if (*ptr == '.') {
            ptr++;
            precision = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                precision = precision * 10 + (*ptr - '0');
                ptr++;
            }
        }
        
        bool has_ll = false;
        bool is_size_t = false;
        
        if (*ptr == 'z') {
            ptr++;
            is_size_t = true;
        } else if (ptr[0] == 'l' && ptr[1] == 'l') {
            has_ll = true;
            ptr += 2;
        } else if (*ptr == 'L' || *ptr == 'l' || *ptr == 'h' || *ptr == 'j' || *ptr == 't') {
            ptr++;
        }
        
        char buffer[MAX_FLOAT_BUFFER];
        int buffer_len = 0;
        
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
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    print_llu_number(num, 10, false);
                    size_t temp = num == 0 ? 1 : 0;
                    size_t n = num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    chars_written += temp;
                } else if (has_ll) {
                    int64_t num = va_arg(args, int64_t);
                    print_signed_ll_number(num, 10, false);
                    int64_t temp = num == 0 ? 1 : 0;
                    int64_t n = num < 0 ? -num : num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    if (num < 0) temp++;
                    chars_written += temp;
                } else {
                    int num = va_arg(args, int);
                    print_signed_number(num, 10, false);
                    int temp = num == 0 ? 1 : 0;
                    int n = num < 0 ? -num : num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    if (num < 0) temp++;
                    chars_written += temp;
                }
                break;
            }
            
            case 'u': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    print_llu_number(num, 10, false);
                    size_t temp = num == 0 ? 1 : 0;
                    size_t n = num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    chars_written += temp;
                } else if (has_ll) {
                    uint64_t num = va_arg(args, uint64_t);
                    print_llu_number(num, 10, false);
                    uint64_t temp = num == 0 ? 1 : 0;
                    uint64_t n = num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    chars_written += temp;
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    print_number(num, 10, false);
                    unsigned int temp = num == 0 ? 1 : 0;
                    unsigned int n = num;
                    while (n != 0) {
                        n /= 10;
                        temp++;
                    }
                    chars_written += temp;
                }
                break;
            }
            
            case 'x': 
            case 'X': {
                bool uppercase_hex = (*ptr == 'X');
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    print_llu_number(num, 16, uppercase_hex);
                    size_t temp = num == 0 ? 1 : 0;
                    size_t n = num;
                    while (n != 0) {
                        n >>= 4;
                        temp++;
                    }
                    chars_written += temp;
                } else if (has_ll) {
                    uint64_t num = va_arg(args, uint64_t);
                    print_llu_number(num, 16, uppercase_hex);
                    uint64_t temp = num == 0 ? 1 : 0;
                    uint64_t n = num;
                    while (n != 0) {
                        n >>= 4;
                        temp++;
                    }
                    chars_written += temp;
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    print_number(num, 16, uppercase_hex);
                    unsigned int temp = num == 0 ? 1 : 0;
                    unsigned int n = num;
                    while (n != 0) {
                        n >>= 4;
                        temp++;
                    }
                    chars_written += temp;
                }
                break;
            }
            
            case 'o': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    print_llu_number(num, 8, false);
                    size_t temp = num == 0 ? 1 : 0;
                    size_t n = num;
                    while (n != 0) {
                        n >>= 3;
                        temp++;
                    }
                    chars_written += temp;
                } else if (has_ll) {
                    uint64_t num = va_arg(args, uint64_t);
                    print_llu_number(num, 8, false);
                    uint64_t temp = num == 0 ? 1 : 0;
                    uint64_t n = num;
                    while (n != 0) {
                        n >>= 3;
                        temp++;
                    }
                    chars_written += temp;
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    print_number(num, 8, false);
                    unsigned int temp = num == 0 ? 1 : 0;
                    unsigned int n = num;
                    while (n != 0) {
                        n >>= 3;
                        temp++;
                    }
                    chars_written += temp;
                }
                break;
            }
            
            case 'f':
            case 'F': {
                double value = va_arg(args, double);
                buffer_len = double_to_string(value, buffer, precision);
                print_string(buffer);
                chars_written += buffer_len;
                break;
            }
            
            case 'e':
            case 'E': {
                double value = va_arg(args, double);
                bool uppercase_e = (*ptr == 'E');
                buffer_len = double_to_scientific(value, buffer, precision, uppercase_e);
                print_string(buffer);
                chars_written += buffer_len;
                break;
            }
            
            case 'g':
            case 'G': {
                double value = va_arg(args, double);
                bool uppercase_g = (*ptr == 'G');
                buffer_len = double_to_general(value, buffer, precision, uppercase_g);
                print_string(buffer);
                chars_written += buffer_len;
                break;
            }
            
            case 'a':
            case 'A': {
                double value = va_arg(args, double);
                bool uppercase_a = (*ptr == 'A');
                buffer_len = double_to_hex(value, buffer, precision, uppercase_a);
                print_string(buffer);
                chars_written += buffer_len;
                break;
            }
            
            case 'p': {
                uintptr_t num = (uintptr_t)va_arg(args, void*);
                print_string("0x");
                print_llu_number(num, 16, false);
                uintptr_t temp = num == 0 ? 1 : 0;
                uintptr_t n = num;
                while (n != 0) {
                    n >>= 4;
                    temp++;
                }
                chars_written += temp + 2;
                break;
            }
            
            case 'n': {
                int *count_ptr = va_arg(args, int*);
                *count_ptr = chars_written;
                break;
            }
            
            case '%': {
                putchar('%');
                chars_written++;
                break;
            }
            
            default: {
                for (const char *p = percent_start; p <= ptr; p++) {
                    putchar(*p);
                    chars_written++;
                }
                break;
            }
        }
        
        ptr++; 
    }
    
    va_end(args);
    return chars_written;
}