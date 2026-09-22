#!/bin/sh
set -e
IN=$1
OUT=$2
{
    echo '#include <stdint.h>'
    echo 'const struct { uint64_t addr; const char *name; } ksyms[] = {'
    nm -n "$IN" | awk '$2 ~ /^[tTwW]$/ && $3 != "" { printf "    { 0x%sULL, \"%s\" },\n", $1, $3 }'
    echo '};'
    echo 'const unsigned long ksyms_count = sizeof(ksyms) / sizeof(ksyms[0]);'
} > "$OUT"
