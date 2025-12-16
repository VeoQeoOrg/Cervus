#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../include/io/ports.h"
#include "../../include/io/serial.h"

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

static void uint_to_str(uint64_t value, char* buffer, int base, bool uppercase) {
    char* ptr = buffer;
    char* ptr1 = buffer;
    char tmp_char;
    
    if (base < 2 || base > 36) {
        *ptr = '\0';
        return;
    }
    
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    
    do {
        int digit = value % base;
        *ptr++ = digits[digit];
        value /= base;
    } while (value > 0);
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
}

void serial_printf(uint16_t port, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[65]; 
    const char* ptr = format;
    
    while (*ptr) {
        if (*ptr != '%') {
            serial_write(port, *ptr);
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
        
        int is_long_long = 0;
        int is_long = 0;
        int is_size_t = 0;
        
        if (*ptr == 'z') {
            ptr++;
            is_size_t = 1;
        } else if (*ptr == 'l') {
            ptr++;
            is_long = 1;
            if (*ptr == 'l') {
                ptr++;
                is_long_long = 1;
                is_long = 0;
            }
        } else if (*ptr == 'h') {
            ptr++;
            if (*ptr == 'h') ptr++;
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
                if (is_size_t) {
                    uint64_t num = va_arg(args, uint64_t);
                    uint_to_str(num, buffer, 10, false);
                } else if (is_long_long) {
                    int64_t num = va_arg(args, int64_t);
                    if (num < 0) {
                        serial_write(port, '-');
                        num = -num;
                    }
                    uint_to_str((uint64_t)num, buffer, 10, false);
                } else if (is_long) {
                    long num = va_arg(args, long);
                    if (num < 0) {
                        serial_write(port, '-');
                        num = -num;
                    }
                    uint_to_str((uint64_t)num, buffer, 10, false);
                } else {
                    int num = va_arg(args, int);
                    if (num < 0) {
                        serial_write(port, '-');
                        num = -num;
                    }
                    uint_to_str((uint64_t)num, buffer, 10, false);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'u': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    uint_to_str(num, buffer, 10, false);
                } else if (is_long_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    uint_to_str(num, buffer, 10, false);
                } else if (is_long) {
                    unsigned long num = va_arg(args, unsigned long);
                    uint_to_str(num, buffer, 10, false);
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    uint_to_str(num, buffer, 10, false);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'x': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    uint_to_str(num, buffer, 16, false);
                } else if (is_long_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    uint_to_str(num, buffer, 16, false);
                } else if (is_long) {
                    unsigned long num = va_arg(args, unsigned long);
                    uint_to_str(num, buffer, 16, false);
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    uint_to_str(num, buffer, 16, false);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'X': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    uint_to_str(num, buffer, 16, true);
                } else if (is_long_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    uint_to_str(num, buffer, 16, true);
                } else if (is_long) {
                    unsigned long num = va_arg(args, unsigned long);
                    uint_to_str(num, buffer, 16, true);
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    uint_to_str(num, buffer, 16, true);
                }
                serial_writestring(port, buffer);
                break;
            }
            
            case 'o': {
                if (is_size_t) {
                    size_t num = va_arg(args, size_t);
                    uint_to_str(num, buffer, 8, false);
                } else if (is_long_long) {
                    uint64_t num = va_arg(args, uint64_t);
                    uint_to_str(num, buffer, 8, false);
                } else if (is_long) {
                    unsigned long num = va_arg(args, unsigned long);
                    uint_to_str(num, buffer, 8, false);
                } else {
                    unsigned int num = va_arg(args, unsigned int);
                    uint_to_str(num, buffer, 8, false);
                }
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
                
                if (isinf(num)) {
                    serial_writestring(port, num < 0 ? "-inf" : "inf");
                    break;
                }
                
                if (num < 0) {
                    serial_write(port, '-');
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
                
                serial_writestring(port, int_ptr);
                
                serial_write(port, '.');
                double frac = num - (int)num;
                
                for (int i = 0; i < 6; i++) {
                    frac *= 10;
                    int digit = (int)frac;
                    serial_write(port, '0' + digit);
                    frac -= digit;
                }
                
                break;
            }
            
            case '%': {
                serial_write(port, '%');
                break;
            }
            
            default: {
                serial_write(port, '%');
                if (*ptr != '\0') {
                    serial_write(port, *ptr);
                }
                break;
            }
        }
        
        if (*ptr != '\0') {
            ptr++;
        }
    }
    
    va_end(args);
}