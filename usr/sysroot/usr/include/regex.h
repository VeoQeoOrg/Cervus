#ifndef _REGEX_H
#define _REGEX_H

#include <stddef.h>

typedef long long regoff_t;

typedef struct {
    size_t  re_nsub;
    void   *__priv;
    int     __cflags;
} regex_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED 0x01
#define REG_ICASE    0x02
#define REG_NOSUB    0x04
#define REG_NEWLINE  0x08

#define REG_NOTBOL   0x01
#define REG_NOTEOL   0x02

#define REG_NOMATCH    1
#define REG_BADPAT     2
#define REG_ECOLLATE   3
#define REG_ECTYPE     4
#define REG_EESCAPE    5
#define REG_ESUBREG    6
#define REG_EBRACK     7
#define REG_EPAREN     8
#define REG_EBRACE     9
#define REG_BADBR      10
#define REG_ERANGE     11
#define REG_ESPACE     12
#define REG_BADRPT     13

int    regcomp(regex_t *preg, const char *pattern, int cflags);
int    regexec(const regex_t *preg, const char *string, size_t nmatch,
               regmatch_t pmatch[], int eflags);
void   regfree(regex_t *preg);
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t buflen);

#endif
