#ifndef _CTYPE_H
#define _CTYPE_H
#ifdef __cplusplus
extern "C" {
#endif

#define _U 01
#define _L 02
#define _N 04
#define _S 010
#define _P 020
#define _C 040
#define _X 0100
#define _B 0200

extern const char _ctype_[257];
extern const char *__ctype_ptr__;

int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isspace(int c);
int isprint(int c);
int isgraph(int c);
int ispunct(int c);
int isxdigit(int c);
int iscntrl(int c);
int isblank(int c);
int toupper(int c);
int tolower(int c);

#ifdef __cplusplus
}
#endif
#endif
