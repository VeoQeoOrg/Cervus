#include <ctype.h>

int iscntrl(int c) { return (unsigned char)c < 0x20 || c == 0x7F; }
