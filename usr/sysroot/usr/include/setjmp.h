#ifndef _SETJMP_H
#define _SETJMP_H

typedef long jmp_buf[8];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#define sigjmp_buf jmp_buf
#define sigsetjmp(env, savesigs) setjmp(env)
#define siglongjmp(env, val)     longjmp(env, val)

#endif