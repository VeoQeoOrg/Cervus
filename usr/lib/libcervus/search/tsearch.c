#include <search.h>
#include <stdlib.h>

#define MAXH (sizeof(void *) * 8 * 3 / 2)

struct node {
    const void *key;
    void *a[2];
    int h;
};

static int height(struct node *n) { return n ? n->h : 0; }

static int rot(void **p, struct node *x, int dir)
{
    struct node *y = x->a[dir];
    struct node *z = y->a[!dir];
    int hx = x->h;
    int hz = height(z);
    if (hz > height(y->a[dir])) {
        x->a[dir] = z->a[!dir];
        y->a[!dir] = z->a[dir];
        z->a[!dir] = x;
        z->a[dir] = y;
        x->h = hz;
        y->h = hz;
        z->h = hz + 1;
    } else {
        x->a[dir] = z;
        y->a[!dir] = x;
        x->h = hz + 1;
        y->h = hz + 2;
        z = y;
    }
    *p = z;
    return z->h - hx;
}

static int balance(void **p)
{
    struct node *n = *p;
    int h0 = height(n->a[0]);
    int h1 = height(n->a[1]);
    if ((unsigned)(h0 - h1 + 1) < 3u) {
        int old = n->h;
        n->h = h0 < h1 ? h1 + 1 : h0 + 1;
        return n->h - old;
    }
    return rot(p, n, h0 < h1);
}

void *tsearch(const void *key, void **rootp, int (*cmp)(const void *, const void *))
{
    if (!rootp) return NULL;
    void **a[MAXH];
    struct node *n = *rootp;
    int i = 0;
    a[i++] = rootp;
    while (n) {
        int c = cmp(key, n->key);
        if (!c) return n;
        a[i++] = &n->a[c > 0];
        n = n->a[c > 0];
    }
    struct node *r = malloc(sizeof *r);
    if (!r) return NULL;
    r->key = key;
    r->a[0] = r->a[1] = NULL;
    r->h = 1;
    *a[--i] = r;
    while (i && balance(a[--i])) { }
    return r;
}

void *tfind(const void *key, void *const *rootp, int (*cmp)(const void *, const void *))
{
    if (!rootp) return NULL;
    struct node *n = *rootp;
    while (n) {
        int c = cmp(key, n->key);
        if (!c) break;
        n = n->a[c > 0];
    }
    return n;
}

void *tdelete(const void *key, void **rootp, int (*cmp)(const void *, const void *))
{
    if (!rootp) return NULL;
    void **a[MAXH + 1];
    struct node *n = *rootp;
    int i = 0;
    a[i++] = rootp;
    a[i++] = rootp;
    for (;;) {
        if (!n) return NULL;
        int c = cmp(key, n->key);
        if (!c) break;
        a[i++] = &n->a[c > 0];
        n = n->a[c > 0];
    }
    struct node *parent = *a[i - 2];
    struct node *child;
    if (n->a[0]) {
        struct node *deleted = n;
        a[i++] = &n->a[0];
        n = n->a[0];
        while (n->a[1]) {
            a[i++] = &n->a[1];
            n = n->a[1];
        }
        deleted->key = n->key;
        child = n->a[0];
    } else {
        child = n->a[1];
    }
    free(n);
    *a[--i] = child;
    while (--i && balance(a[i])) { }
    return parent;
}

static void walk(const struct node *r, void (*action)(const void *, VISIT, int), int d)
{
    if (!r) return;
    if (r->h == 1) {
        action(r, leaf, d);
        return;
    }
    action(r, preorder, d);
    walk(r->a[0], action, d + 1);
    action(r, postorder, d);
    walk(r->a[1], action, d + 1);
    action(r, endorder, d);
}

void twalk(const void *root, void (*action)(const void *, VISIT, int))
{
    walk(root, action, 0);
}

void tdestroy(void *root, void (*freenode)(void *))
{
    struct node *r = root;
    if (!r) return;
    tdestroy(r->a[0], freenode);
    tdestroy(r->a[1], freenode);
    if (freenode) freenode((void *)r->key);
    free(r);
}
