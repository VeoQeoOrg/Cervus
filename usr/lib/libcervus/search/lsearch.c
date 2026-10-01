#include <search.h>
#include <string.h>

void *lfind(const void *key, const void *base, size_t *nelp, size_t width,
            int (*compar)(const void *, const void *))
{
    const char *p = base;
    for (size_t i = 0; i < *nelp; i++, p += width)
        if (!compar(key, p)) return (void *)p;
    return NULL;
}

void *lsearch(const void *key, void *base, size_t *nelp, size_t width,
              int (*compar)(const void *, const void *))
{
    void *r = lfind(key, base, nelp, width, compar);
    if (r) return r;
    char *dst = (char *)base + *nelp * width;
    memcpy(dst, key, width);
    (*nelp)++;
    return dst;
}

void insque(void *elem, void *prev)
{
    struct qelem *e = elem, *p = prev;
    if (!p) {
        e->q_forw = e->q_back = NULL;
        return;
    }
    e->q_forw = p->q_forw;
    e->q_back = p;
    if (p->q_forw) p->q_forw->q_back = e;
    p->q_forw = e;
}

void remque(void *elem)
{
    struct qelem *e = elem;
    if (e->q_forw) e->q_forw->q_back = e->q_back;
    if (e->q_back) e->q_back->q_forw = e->q_forw;
}
