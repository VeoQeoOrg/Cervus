#include <ctype.h>

int iscntrl(int c) {
    return c == '\n' || c == '\t' ||
        c == '\r' || c == '\b' ||
        c == '\f' || c == '\a' || 
        c == '\0';
}
