#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

TARGET=x86_64-cervus
CROSS=$ROOT/usr/cross
SRC=$CROSS/src
BUILD=$CROSS/build
PREFIX=$CROSS/tools
SYSROOT=$ROOT/usr/sysroot
BINUTILS=binutils-2.45
GCC=gcc-15.2.0
JOBS=$(nproc 2>/dev/null || echo 4)

say() { printf '\033[92m[cross]\033[0m %s\n' "$*"; }

insert_before() {
    _f=$1; _m=$2; _a=$3; _t=$4
    grep -qF "$_m" "$_f" && return 0
    awk -v anchor="$_a" -v text="$_t" '
        index($0, anchor) && !done { printf "%s\n", text; done=1 }
        { print }
    ' "$_f" > "$_f.tmp" && cat "$_f.tmp" > "$_f" && rm -f "$_f.tmp"
    grep -qF "$_m" "$_f" || { echo "PATCH FAILED: $_m in $_f" >&2; exit 1; }
}

for cs in "$SRC/$BINUTILS/config.sub" "$SRC/$GCC/config.sub"; do
    insert_before "$cs" "| cervus*" "| nsk*" "\t| cervus* \\\\"
    chmod +x "$cs"
done
say "config.sub patched"

insert_before "$SRC/$BINUTILS/bfd/config.bfd" "x86_64-*-cervus" \
    "x86_64-*-elf* | x86_64-*-rtems" \
    "  x86_64-*-cervus*)\n    targ_defvec=x86_64_elf64_vec\n    targ_selvecs=i386_elf32_vec\n    want64=true\n    ;;"
insert_before "$SRC/$BINUTILS/gas/configure.tgt" "cervus" \
    "i386-*-elf*)" \
    "  i386-*-cervus* | x86_64-*-cervus*)\tfmt=elf ;;"
insert_before "$SRC/$BINUTILS/ld/configure.tgt" "x86_64-*-cervus" \
    "x86_64-*-elf* | x86_64-*-rtems" \
    "x86_64-*-cervus*)\ttarg_emul=elf_x86_64\n\t\t\ttarg_extra_emuls=\"elf_i386\"\n\t\t\t;;"
say "binutils target wired"

insert_before "$SRC/$GCC/gcc/config.gcc" "x86_64-*-cervus" \
    "x86_64-*-elf*)" \
    "x86_64-*-cervus*)\n\ttm_file=\"\${tm_file} i386/unix.h i386/att.h elfos.h newlib-stdint.h i386/i386elf.h i386/x86-64.h cervus.h\"\n\tgas=yes\n\tgnu_ld=yes\n\tuse_gcc_stdint=wrap\n\tdefault_use_cxa_atexit=yes\n\t;;"
insert_before "$SRC/$GCC/libgcc/config.host" "x86_64-*-cervus" \
    "x86_64-*-elf* | x86_64-*-rtems" \
    "x86_64-*-cervus*)\n\ttmake_file=\"\$tmake_file i386/t-crtstuff t-crtstuff-pic t-libgcc-pic\"\n\t;;"
cp "$ROOT/builder/cross/cervus.h" "$SRC/$GCC/gcc/config/cervus.h"
say "gcc target wired (cervus.h + libgcc/config.host)"

mkdir -p "$BUILD/binutils" "$PREFIX"
if [ ! -x "$PREFIX/bin/$TARGET-ld" ]; then
    say "configuring + building binutils ($JOBS jobs)"
    cd "$BUILD/binutils"
    "$SRC/$BINUTILS/configure" --target=$TARGET --prefix="$PREFIX" \
        --with-sysroot="$SYSROOT" --disable-nls --disable-werror \
        --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng \
        >config.log 2>&1
    make -j"$JOBS" >build.log 2>&1
    make install >>build.log 2>&1
    say "binutils installed"
else
    say "binutils already built"
fi

export PATH="$PREFIX/bin:$PATH"

mkdir -p "$BUILD/gcc"
if [ ! -x "$PREFIX/bin/$TARGET-gcc" ]; then
    say "configuring gcc"
    cd "$BUILD/gcc"
    "$SRC/$GCC/configure" --target=$TARGET --prefix="$PREFIX" \
        --with-sysroot="$SYSROOT" --disable-nls --disable-werror \
        --enable-languages=c --disable-multilib --disable-shared \
        --disable-threads --disable-libssp --disable-libgomp \
        --disable-libatomic --disable-libquadmath --disable-libvtv \
        CXX="g++ -std=gnu++17" CXXFLAGS_FOR_BUILD="-std=gnu++17 -O2" \
        >config.log 2>&1
    say "building gcc (all-gcc, $JOBS jobs)"
    make -j"$JOBS" all-gcc >build.log 2>&1
    make install-gcc >>build.log 2>&1
    say "building libgcc"
    make -j"$JOBS" all-target-libgcc >>build.log 2>&1
    make install-target-libgcc >>build.log 2>&1
    say "gcc + libgcc installed"
else
    say "gcc already built"
fi

say "DONE. Toolchain: $PREFIX/bin/$TARGET-gcc"
"$PREFIX/bin/$TARGET-gcc" --version | head -1 || true
