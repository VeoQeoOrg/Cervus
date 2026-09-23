#include <string.h>
#include <ctype.h>

int strverscmp(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    while (*x && *x == *y && !isdigit(*x)) {
        x++;
        y++;
    }
    while (*x || *y) {
        if (isdigit(*x) && isdigit(*y)) {
            const unsigned char *xs = x, *ys = y;
            int xfrac = *x == '0', yfrac = *y == '0';
            while (isdigit(*x)) x++;
            while (isdigit(*y)) y++;
            size_t xl = (size_t)(x - xs), yl = (size_t)(y - ys);
            if (xfrac || yfrac) {
                size_t n = xl < yl ? xl : yl;
                int c = memcmp(xs, ys, n);
                if (c) return c;
                if (xl != yl) return xfrac && yfrac ? (xl < yl ? 1 : -1) : (xl < yl ? -1 : 1);
            } else {
                if (xl != yl) return xl < yl ? -1 : 1;
                int c = memcmp(xs, ys, xl);
                if (c) return c;
            }
            continue;
        }
        if (*x != *y) return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return 0;
}
