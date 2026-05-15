#include <ctype.h>

int isgraph(int c) { return (unsigned char)c > 0x20 && (unsigned char)c < 0x7F; }
