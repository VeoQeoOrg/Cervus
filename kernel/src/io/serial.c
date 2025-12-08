#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        
        if (*ptr == 'l') {
            ptr++;
            if (*ptr == 'l') ptr++;
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
                int num = va_arg(args, int);
                itoa(num, buffer, 10); 
                serial_writestring(port, buffer);
                break;
            }
            
            case 'u': {
                unsigned int num = va_arg(args, unsigned int);
                uint_to_str(num, buffer, 10, false);
                serial_writestring(port, buffer);
                break;
            }
            
            case 'x': {
                unsigned int num = va_arg(args, unsigned int);
                uint_to_str(num, buffer, 16, false);
                serial_writestring(port, buffer);
                break;
            }
            
            case 'X': {
                unsigned int num = va_arg(args, unsigned int);
                uint_to_str(num, buffer, 16, true);
                serial_writestring(port, buffer);
                break;
            }
            
            case 'o': {
                unsigned int num = va_arg(args, unsigned int);
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