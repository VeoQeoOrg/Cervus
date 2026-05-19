#ifndef _CERVUS_REGEX_INTERNAL_H
#define _CERVUS_REGEX_INTERNAL_H

#include <regex.h>
#include <stddef.h>
#include <stdint.h>

#define RE_MAX_GROUPS 32

enum {
    OP_CHAR,
    OP_ANY,
    OP_CLASS,
    OP_NCLASS,
    OP_BOL,
    OP_EOL,
    OP_WBOUND,
    OP_NWBOUND,
    OP_GSTART,
    OP_GEND,
    OP_BACKREF,
    OP_JMP,
    OP_SPLIT,
    OP_MATCH
};

typedef struct re_inst {
    uint16_t op;
    int32_t  x;
    int32_t  y;
    uint32_t cls_off;
} re_inst_t;

typedef struct {
    re_inst_t *code;
    size_t     code_len;
    size_t     code_cap;

    uint8_t   *cls;
    size_t     cls_len;
    size_t     cls_cap;

    size_t     ngroup;
    int        cflags;
} re_prog_t;

#endif
