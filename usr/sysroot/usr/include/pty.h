#ifndef _CERVUS_PTY_H
#define _CERVUS_PTY_H
#ifdef __cplusplus
extern "C" {
#endif

int openpty(int *master, int *slave);

#ifdef __cplusplus
}
#endif
#endif
