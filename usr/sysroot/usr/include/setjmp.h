#ifndef _SETJMP_H
#define _SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif

typedef long jmp_buf[8];

int  setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

int  _setjmp(jmp_buf env) __attribute__((returns_twice));
void _longjmp(jmp_buf env, int val) __attribute__((noreturn));

typedef long sigjmp_buf[12];

int  sigsetjmp(sigjmp_buf env, int savesigs) __attribute__((returns_twice));
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
#endif