#!/bin/sh
set -eu
src=$1
out=$2
stem=$(basename "$src" .psf)
tmp="temp_${stem}.psf"

cp "$src" "$tmp"
objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$tmp" "$out"
objcopy \
    --redefine-sym "_binary_temp_${stem}_psf_start=_binary_${stem}_psf_start" \
    --redefine-sym "_binary_temp_${stem}_psf_end=_binary_${stem}_psf_end" \
    --redefine-sym "_binary_temp_${stem}_psf_size=_binary_${stem}_psf_size" \
    "$out"
rm -f "$tmp"
