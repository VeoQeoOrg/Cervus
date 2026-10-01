#ifndef _SEARCH_H
#define _SEARCH_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef enum { FIND, ENTER } ACTION;
typedef enum { preorder, postorder, endorder, leaf } VISIT;

typedef struct entry {
    char *key;
    void *data;
} ENTRY;

struct qelem {
    struct qelem *q_forw;
    struct qelem *q_back;
    char q_data[1];
};

int    hcreate(size_t nel);
void   hdestroy(void);
ENTRY *hsearch(ENTRY item, ACTION action);

void   insque(void *elem, void *prev);
void   remque(void *elem);

void  *lsearch(const void *key, void *base, size_t *nelp, size_t width,
               int (*compar)(const void *, const void *));
void  *lfind(const void *key, const void *base, size_t *nelp, size_t width,
             int (*compar)(const void *, const void *));

void  *tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *));
void  *tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *));
void  *tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *));
void   twalk(const void *root, void (*action)(const void *, VISIT, int));
void   tdestroy(void *root, void (*freenode)(void *));

#ifdef __cplusplus
}
#endif
#endif
