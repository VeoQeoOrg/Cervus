#!/bin/sh

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

say() { printf '\033[96m[tcc]\033[0m %s\n' "$*"; }
ok()  { printf '\033[92m[tcc]\033[0m %s\n' "$*"; }
die() { printf '\033[91m[tcc] %s\033[0m\n' "$*" >&2; exit 1; }

TCC_VERSION=0.9.27
TCC_TARBALL=tcc-$TCC_VERSION.tar.bz2
TCC_SRC_DIRNAME=tcc-$TCC_VERSION
TCC_URL="https://download-mirror.savannah.gnu.org/releases/tinycc/$TCC_TARBALL"

TCC_DIR=usr/tcc
SRC_DIR=$TCC_DIR/$TCC_SRC_DIRNAME

SYSROOT_DIR=usr/sysroot
SYSROOT_INC=$SYSROOT_DIR/usr/include
SYSROOT_LIB=$SYSROOT_DIR/usr/lib

TCC_ELF=$TCC_DIR/tcc.elf
LIBTCC1_A=$TCC_DIR/libtcc1.a

mkdir -p "$TCC_DIR"
if [ ! -s "$TCC_DIR/$TCC_TARBALL" ]; then
    say "downloading $TCC_TARBALL"
    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$TCC_DIR/$TCC_TARBALL" "$TCC_URL"
    elif command -v curl >/dev/null 2>&1; then
        curl -fL -o "$TCC_DIR/$TCC_TARBALL" "$TCC_URL"
    else
        die "need wget or curl to download tcc"
    fi
fi

if [ ! -d "$SRC_DIR" ]; then
    say "extracting $TCC_TARBALL"
    tar -xjf "$TCC_DIR/$TCC_TARBALL" -C "$TCC_DIR"
fi

cat > "$SRC_DIR/config.h" <<EOF
#ifndef _CONFIG_H
#define _CONFIG_H

#define TCC_VERSION "$TCC_VERSION"

#define CONFIG_TCC_SYSROOT      ""
#define CONFIG_TCC_LIBPATHS     "/usr/lib"
#define CONFIG_TCC_CRTPREFIX    "/usr/lib"
#define CONFIG_TCC_ELFINTERP    ""
#define CONFIG_TCCDIR           "/usr/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/lib/tcc/include" ":" "/usr/local/include" ":" "/usr/include"

#define HOST_OS    "Cervus"
#define HOST_ARCH  "x86_64"

#define TCC_TARGET_X86_64 1

#define CONFIG_TCC_PREDEFS  1
#define CONFIG_TCC_STATIC   1

#define TCC_NO_DLOPEN    1
#define TCC_NO_BACKTRACE 1

#define CONFIG_TCC_BCHECK 0

#undef  CONFIG_WIN32
#undef  CONFIG_WIN64
#undef  TCC_TARGET_PE

#endif
EOF

say "applying Cervus patches"
perl "$ROOT/builder/tcc_patch.pl" "$SRC_DIR"

CRT0=$SYSROOT_LIB/crt0.o
[ -f "$CRT0" ] || die "$CRT0 missing (libcervus not built yet)"

need_tcc_elf=1
if [ -f "$TCC_ELF" ]; then
    need_tcc_elf=0
    for f in "$SRC_DIR/tcc.c" "$SRC_DIR/tcc.h" "$SRC_DIR/libtcc.c" \
             "$SRC_DIR/tccelf.c" "$SRC_DIR/tccrun.c" "$SRC_DIR/tccgen.c" \
             "$SRC_DIR/x86_64-link.c" "$SRC_DIR/x86_64-gen.c" "$SRC_DIR/tccpp.c"; do
        [ -f "$f" ] && [ "$f" -nt "$TCC_ELF" ] && need_tcc_elf=1
    done
fi

if [ "$need_tcc_elf" -eq 1 ]; then
    say "building tcc.elf for Cervus (x86_64)"
    gcc -ffreestanding -nostdlib -static -fno-stack-protector \
        -mno-red-zone -fno-pie -fno-pic -O2 -D__CERVUS__ \
        -nostdinc -isystem "$ROOT/$SYSROOT_INC" \
        -DTCC_TARGET_X86_64 -DONE_SOURCE=1 \
        -DCONFIG_TCC_PREDEFS=1 -DTCC_NO_DLOPEN=1 -DTCC_NO_BACKTRACE=1 \
        -DCONFIG_TCC_STATIC=1 \
        -I"$SRC_DIR" \
        -o "$TCC_ELF" "$SRC_DIR/tcc.c" "$CRT0" \
        -nostdlib -static -L"$ROOT/$SYSROOT_LIB" -lcervus
else
    ok "tcc.elf up to date"
fi

if [ ! -f "$LIBTCC1_A" ]; then
    say "building libtcc1.a"
    l1_flags="-ffreestanding -nostdlib -fno-stack-protector \
-mno-red-zone -fno-pie -fno-pic -O2 -D__CERVUS__ -DTCC_TARGET_X86_64"

    [ -f "$SRC_DIR/lib/libtcc1.c" ] || die "missing $SRC_DIR/lib/libtcc1.c"
    gcc $l1_flags -c "$SRC_DIR/lib/libtcc1.c" -o "$TCC_DIR/libtcc1.o"
    ar_objs="$TCC_DIR/libtcc1.o"

    if [ -f "$SRC_DIR/lib/alloca86_64.S" ]; then
        gcc $l1_flags -c "$SRC_DIR/lib/alloca86_64.S" -o "$TCC_DIR/alloca86_64.o"
        ar_objs="$ar_objs $TCC_DIR/alloca86_64.o"
    fi
    if [ -f "$SRC_DIR/lib/va_list.c" ]; then
        gcc $l1_flags -c "$SRC_DIR/lib/va_list.c" -o "$TCC_DIR/va_list.o"
        ar_objs="$ar_objs $TCC_DIR/va_list.o"
    fi

    ar rcs "$LIBTCC1_A" $ar_objs
else
    ok "libtcc1.a up to date"
fi

say "installing to sysroot"
mkdir -p "$SYSROOT_DIR/usr/bin" "$SYSROOT_DIR/usr/lib/tcc/include"
cp "$TCC_ELF" "$SYSROOT_DIR/usr/bin/tcc"
cp "$LIBTCC1_A" "$SYSROOT_DIR/usr/lib/tcc/libtcc1.a"
cp "$SRC_DIR"/include/*.h "$SYSROOT_DIR/usr/lib/tcc/include/"

ok "installed to $SYSROOT_DIR/usr/bin/tcc"
