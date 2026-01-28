#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"

static volatile uint8_t serial_lock = 0;

void serial_initialize(uint16_t port, uint32_t baud_rate) {
    outb(port + 1, 0x00);

    uint16_t divisor = 115200 / baud_rate;
    outb(port + 3, 0x80);
    outb(port + 0, divisor & 0xFF);
    outb(port + 1, (divisor >> 8) & 0xFF);

    outb(port + 3, 0x03);

    outb(port + 2, 0xC7);

    outb(port + 4, 0x0B);

    outb(port + 4, 0x1E);
    outb(port + 0, 0xAE);

    if (inb(port + 0) != 0xAE) {
        return;
    }

    outb(port + 4, 0x0F);
}

int serial_received(uint16_t port) {
    return inb(port + 5) & 1;
}

char serial_read(uint16_t port) {
    while (serial_received(port) == 0);
    return inb(port);
}

int serial_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

void serial_write(uint16_t port, char c) {
    while (serial_is_transmit_empty(port) == 0);
    outb(port, c);
}

void serial_writestring(uint16_t port, const char* str) {
    while (*str) {
        serial_write(port, *str++);
    }
}

static void reverse_string(char* str, int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

static void uint_to_str(uint64_t value, char* buffer, int base, bool uppercase) {
    char* ptr = buffer;

    if (base < 2 || base > 36) {
        *ptr = '\0';
        return;
    }

    const char* digits = uppercase ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" :
                                     "0123456789abcdefghijklmnopqrstuvwxyz";

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return;
    }

    while (value > 0) {
        int digit = value % base;
        *ptr++ = digits[digit];
        value /= base;
    }

    *ptr = '\0';
    reverse_string(buffer, ptr - buffer);
}

static void int_to_str(int64_t value, char* buffer, int base, bool uppercase) {
    if (value < 0 && (base == 10 || base == 8)) {
        buffer[0] = '-';
        uint_to_str(-value, buffer + 1, base, uppercase);
    } else {
        uint_to_str(value, buffer, base, uppercase);
    }
}

static uint64_t pow10_u64(int n) {
    uint64_t r = 1;
    while (n-- > 0) r *= 10;
    return r;
}

static int double_to_string(double value, char* buffer, int precision) {
    if (isnan(value)) {
        strcpy(buffer, "nan");
        return 3;
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

    if (precision < 0) precision = 6;
    if (precision > 16) precision = 16;

    bool negative = (value < 0);
    double abs_val = negative ? -value : value;

    uint64_t mult = pow10_u64(precision);
    double scaled = abs_val * mult + 0.5;
    uint64_t int_scaled = (uint64_t)scaled;

    uint64_t int_part = int_scaled / mult;
    uint64_t frac_part = int_scaled % mult;

    char* ptr = buffer;
    if (negative) *ptr++ = '-';

    char int_buf[32];
    char* p = int_buf + sizeof(int_buf) - 1;
    *p = '\0';

    if (int_part == 0) {
        *--p = '0';
    } else {
        uint64_t n = int_part;
        while (n > 0) {
            *--p = '0' + (n % 10);
            n /= 10;
        }
    }

    size_t int_len = (int_buf + sizeof(int_buf) - 1) - p;
    memcpy(ptr, p, int_len);
    ptr += int_len;

    if (precision > 0) {
        *ptr++ = '.';

        char frac_buf[32];
        char* f = frac_buf + sizeof(frac_buf) - 1;
        *f = '\0';

        if (frac_part == 0) {
            for (int i = 0; i < precision; i++) {
                *--f = '0';
            }
        } else {
            uint64_t n = frac_part;
            int count = 0;
            while (n > 0) {
                *--f = '0' + (n % 10);
                n /= 10;
                count++;
            }
            while (count < precision) {
                *--f = '0';
                count++;
            }
        }

        size_t frac_len = (frac_buf + sizeof(frac_buf) - 1) - f;
        memcpy(ptr, f, frac_len);
        ptr += frac_len;
    }

    *ptr = '\0';
    return ptr - buffer;
}

static int double_to_scientific(double value, char* buffer, int precision, bool uppercase) {
    if (isnan(value)) {
        strcpy(buffer, "nan");
        return 3;
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

    if (value == 0.0) {
        if (precision < 0) precision = 6;
        buffer[0] = '0';
        buffer[1] = '.';
        for (int i = 0; i < precision; i++) {
            buffer[2 + i] = '0';
        }
        buffer[2 + precision] = uppercase ? 'E' : 'e';
        buffer[3 + precision] = '+';
        buffer[4 + precision] = '0';
        buffer[5 + precision] = '0';
        buffer[6 + precision] = '\0';
        return 6 + precision;
    }

    if (precision < 0) precision = 6;
    if (precision > 16) precision = 16;

    double abs_val = value < 0 ? -value : value;
    int exponent = 0;

    if (abs_val >= 10.0) {
        while (abs_val >= 10.0) {
            abs_val /= 10.0;
            exponent++;
        }
    } else if (abs_val < 1.0 && abs_val > 0.0) {
        while (abs_val < 1.0) {
            abs_val *= 10.0;
            exponent--;
        }
    }

    if (value < 0) abs_val = -abs_val;

    int len = double_to_string(abs_val, buffer, precision);

    char e_char = uppercase ? 'E' : 'e';
    buffer[len++] = e_char;

    if (exponent >= 0) {
        buffer[len++] = '+';
    } else {
        buffer[len++] = '-';
        exponent = -exponent;
    }

    if (exponent < 10) {
        buffer[len++] = '0';
        buffer[len++] = '0' + exponent;
    } else {
        buffer[len++] = '0' + (exponent / 10);
        buffer[len++] = '0' + (exponent % 10);
    }

    buffer[len] = '\0';
    return len;
}

static int double_to_general(double value, char* buffer, int precision, bool uppercase) {
    double abs_val = value < 0 ? -value : value;

    if (abs_val == 0.0) {
        strcpy(buffer, "0");
        return 1;
    }

    if (precision < 0) precision = 6;
    if (precision == 0) precision = 1;

    bool use_scientific = (abs_val >= 1e6 || (abs_val < 1e-4 && abs_val > 0));

    if (use_scientific) {
        return double_to_scientific(value, buffer, precision - 1, uppercase);
    }

    int digits_before = 0;
    double temp = abs_val;
    while (temp >= 1.0) {
        temp /= 10.0;
        digits_before++;
    }
    if (digits_before == 0) digits_before = 1;

    int decimal_places = precision - digits_before;
    if (decimal_places < 0) decimal_places = 0;

    return double_to_string(value, buffer, decimal_places);
}

void serial_printf(uint16_t port, const char* format, ...) {
    while (__sync_lock_test_and_set(&serial_lock, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }

    va_list args;
    va_start(args, format);

    char buffer[256];
    const char* ptr = format;

    while (*ptr) {
        if (*ptr != '%') {
            serial_write(port, *ptr);
            ptr++;
            continue;
        }

        const char* percent_start = ptr++;

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
        bool has_l = false;
        bool has_size_t = false;

        if (*ptr == 'z') {
            ptr++;
            has_size_t = true;
        } else if (*ptr == 'l') {
            ptr++;
            if (*ptr == 'l') {
                ptr++;
                has_ll = true;
            } else {
                has_l = true;
            }
        } else if (*ptr == 'h') {
            ptr++;
            if (*ptr == 'h') ptr++;
        } else if (*ptr == 'L' || *ptr == 'j' || *ptr == 't') {
            ptr++;
        }

        switch (*ptr) {
            case 'c': {
                char c = (char)va_arg(args, int);
                serial_write(port, c);
                break;
            }

            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) {
                    str = "(null)";
                }
                serial_writestring(port, str);
                break;
            }

            case 'd':
            case 'i': {
                int64_t num;
                if (has_size_t) {
                    num = (int64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, int64_t);
                } else if (has_l) {
                    num = (int64_t)va_arg(args, long);
                } else {
                    num = (int64_t)va_arg(args, int);
                }
                int_to_str(num, buffer, 10, false);
                serial_writestring(port, buffer);
                break;
            }

            case 'u': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 10, false);
                serial_writestring(port, buffer);
                break;
            }

            case 'x': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 16, false);
                serial_writestring(port, buffer);
                break;
            }

            case 'X': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 16, true);
                serial_writestring(port, buffer);
                break;
            }

            case 'o': {
                uint64_t num;
                if (has_size_t) {
                    num = (uint64_t)va_arg(args, size_t);
                } else if (has_ll) {
                    num = va_arg(args, uint64_t);
                } else if (has_l) {
                    num = (uint64_t)va_arg(args, unsigned long);
                } else {
                    num = (uint64_t)va_arg(args, unsigned int);
                }
                uint_to_str(num, buffer, 8, false);
                serial_writestring(port, buffer);
                break;
            }

            case 'p': {
                void* ptr_val = va_arg(args, void*);
                serial_writestring(port, "0x");
                uint_to_str((uintptr_t)ptr_val, buffer, 16, false);
                serial_writestring(port, buffer);
                break;
            }

            case 'f':
            case 'F': {
                double num = va_arg(args, double);
                double_to_string(num, buffer, precision);
                serial_writestring(port, buffer);
                break;
            }

            case 'e':
            case 'E': {
                double num = va_arg(args, double);
                double_to_scientific(num, buffer, precision, (*ptr == 'E'));
                serial_writestring(port, buffer);
                break;
            }

            case 'g':
            case 'G': {
                double num = va_arg(args, double);
                double_to_general(num, buffer, precision, (*ptr == 'G'));
                serial_writestring(port, buffer);
                break;
            }

            case 'a':
            case 'A': {
                double num = va_arg(args, double);
                double_to_scientific(num, buffer, precision, (*ptr == 'A'));
                buffer[0] = '0';
                buffer[1] = 'x';
                serial_writestring(port, buffer);
                break;
            }

            case 'n': {
                int* count_ptr = va_arg(args, int*);
                *count_ptr = 0;
                break;
            }

            case '%': {
                serial_write(port, '%');
                break;
            }

            default: {
                for (const char* p = percent_start; p <= ptr; p++) {
                    serial_write(port, *p);
                }
                break;
            }
        }

        if (*ptr != '\0') {
            ptr++;
        }
    }

    va_end(args);
    __sync_lock_release(&serial_lock);
}