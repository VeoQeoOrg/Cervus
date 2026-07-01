#!/bin/sh
# One-time / network / source-patching setup, split out of the main build so
# Ninja can gate each piece behind a stamp file and never repeat it.
#
#   builder/bootstrap.sh deps     fetch freestnd-c-hdrs, cc-runtime, limine-protocol
#   builder/bootstrap.sh limine   download + build the Limine bootloader
#   builder/bootstrap.sh tcc      build the on-OS tcc compiler into the sysroot
#
# The tcc step deliberately reuses the legacy builder (builder/build.c) for its
# ~400 lines of fragile in-place source patching -- there is no point
# reimplementing that here.

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

say() { printf '\033[96m[bootstrap]\033[0m %s\n' "$*"; }
die() { printf '\033[91m[bootstrap] %s\033[0m\n' "$*" >&2; exit 1; }

LIMINE_VERSION=12.3.3
LIMINE_TARBALL=limine-binary.tar.gz
LIMINE_URL="https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/${LIMINE_TARBALL}"

dep_clone() {  # $1=name  $2=url  $3=commit
    dir="limine-tools/$1"
    [ -d "$dir" ] && { say "$1 present"; return 0; }
    say "cloning $1"
    git clone "$2" "$dir"
    git -C "$dir" -c advice.detachedHead=false checkout "$3"
}

do_deps() {
    mkdir -p limine-tools
    dep_clone freestnd-c-hdrs \
        https://codeberg.org/OSDev/freestnd-c-hdrs-0bsd.git \
        5df91dd7062ad0c54f5ffd86193bb9f008677631
    dep_clone cc-runtime \
        https://codeberg.org/OSDev/cc-runtime.git \
        dae79833b57a01b9fd3e359ee31def69f5ae899b
    dep_clone limine-protocol \
        https://codeberg.org/Limine/limine-protocol.git \
        c4616df2572d77c60020bdefa617dd9bdcc6566a
}

extract_hdd_bin() {
    src=limine/limine-bios-hdd.h
    dst=limine/limine-bios-hdd.bin
    [ -f "$dst" ] && return 0
    [ -f "$src" ] || return 0
    if command -v xxd >/dev/null 2>&1; then
        grep -oE '0x[0-9a-fA-F]{2}' "$src" | sed 's/0x//' | tr -d '\n' \
            | xxd -r -p > "$dst"
        say "extracted $(basename "$dst")"
    else
        say "xxd not found -- skipping limine-bios-hdd.bin (optional)"
    fi
}

do_limine() {
    if [ -f limine/limine ]; then
        say "limine already built"
        extract_hdd_bin
        return 0
    fi
    say "fetching Limine v$LIMINE_VERSION"
    rm -rf limine "$LIMINE_TARBALL"
    curl -fL --retry 3 -o "$LIMINE_TARBALL" "$LIMINE_URL"
    tar -xzf "$LIMINE_TARBALL"
    rm -f "$LIMINE_TARBALL"
    [ -f limine-binary/Makefile ] || die "tarball missing limine-binary/Makefile"
    mv limine-binary limine
    make -C limine
    extract_hdd_bin
}

do_tcc() {
    say "building tcc via host builder"
    # `_tcc` rebuilds libcervus as a side effect, clobbering the sysroot
    # libcervus.a / crt0.o that Ninja owns and bumping their mtimes -- which
    # would make every app relink forever. Snapshot them (content + mtime) and
    # restore afterward so Ninja sees no change.
    saved=""
    for f in usr/sysroot/usr/lib/libcervus.a usr/sysroot/usr/lib/crt0.o; do
        if [ -f "$f" ]; then cp -p "$f" "$f.fzsave"; saved="$saved $f"; fi
    done
    restore() { for f in $saved; do [ -f "$f.fzsave" ] && mv "$f.fzsave" "$f"; done; }
    trap 'restore' EXIT

    # Prefer the ForgeZero-built ./build (see .fz.yaml); fall back to a private
    # copy so `./nb` works even when the build wasn't driven through fz.
    if [ -f ./build ] && [ -x ./build ]; then
        ./build _tcc
    else
        legacy=builder/.legacy-build
        if [ ! -x "$legacy" ] || [ builder/build.c -nt "$legacy" ]; then
            cc -O2 -o "$legacy" builder/build.c
        fi
        "./$legacy" _tcc
    fi
    restore
    trap - EXIT
}

[ $# -eq 1 ] || die "usage: bootstrap.sh {deps|limine|tcc}"
case "$1" in
    deps)   do_deps ;;
    limine) do_limine ;;
    tcc)    do_tcc ;;
    *)      die "unknown step: $1" ;;
esac
