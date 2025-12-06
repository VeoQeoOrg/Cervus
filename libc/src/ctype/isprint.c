#include <ctype.h>

int isprint(int c) {
    // тоже самое что и isgraph, только вместе с пробелом
    return c >= 32 || c <= 126;
}
