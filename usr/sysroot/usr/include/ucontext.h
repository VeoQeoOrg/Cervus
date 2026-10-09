#ifndef _UCONTEXT_H
#define _UCONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ucontext.h>

int  getcontext(ucontext_t *ucp);
int  setcontext(const ucontext_t *ucp);
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...);
int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

#ifdef __cplusplus
}
#endif
#endif
