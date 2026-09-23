#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int scandir(const char *path, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent **list = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (filter && !filter(de)) continue;
        if (count == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            struct dirent **nl = realloc(list, ncap * sizeof *nl);
            if (!nl) goto fail;
            list = nl;
            cap = ncap;
        }
        struct dirent *copy = malloc(sizeof *copy);
        if (!copy) goto fail;
        memcpy(copy, de, sizeof *copy);
        list[count++] = copy;
    }
    closedir(d);
    if (compar && count > 1)
        qsort(list, count, sizeof *list, (int (*)(const void *, const void *))compar);
    *namelist = list;
    return (int)count;

fail:
    while (count > 0) free(list[--count]);
    free(list);
    closedir(d);
    return -1;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
    return strcoll((*a)->d_name, (*b)->d_name);
}

int versionsort(const struct dirent **a, const struct dirent **b)
{
    const unsigned char *x = (const unsigned char *)(*a)->d_name;
    const unsigned char *y = (const unsigned char *)(*b)->d_name;
    while (*x && *y) {
        if (isdigit(*x) && isdigit(*y)) {
            while (*x == '0') x++;
            while (*y == '0') y++;
            const unsigned char *xs = x, *ys = y;
            while (isdigit(*x)) x++;
            while (isdigit(*y)) y++;
            size_t xl = (size_t)(x - xs), yl = (size_t)(y - ys);
            if (xl != yl) return xl < yl ? -1 : 1;
            int c = memcmp(xs, ys, xl);
            if (c) return c;
            continue;
        }
        if (*x != *y) return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return (*x > *y) - (*x < *y);
}
