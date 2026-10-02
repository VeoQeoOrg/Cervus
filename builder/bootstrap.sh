#!/bin/sh

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

say() { printf '\033[96m[bootstrap]\033[0m %s\n' "$*"; }
die() { printf '\033[91m[bootstrap] %s\033[0m\n' "$*" >&2; exit 1; }

LIMINE_VERSION=12.9.0
LIMINE_TARBALL=limine-binary.tar.gz
LIMINE_URL="https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/${LIMINE_TARBALL}"

dep_clone() {
    dir="limine-tools/$1"
    [ -d "$dir" ] && { say "$1 present"; return 0; }
    say "cloning $1"
    git clone "$2" "$dir"
    git -C "$dir" -c advice.detachedHead=false checkout "$3"
}

do_deps() {
    mkdir -p limine-tools
    dep_clone freestnd-c-hdrs \
        https://github.com/osdev0/freestanding-c-hdrs.git \
        5df91dd7062ad0c54f5ffd86193bb9f008677631
    dep_clone cc-runtime \
        https://github.com/osdev0/cc-runtime.git \
        dae79833b57a01b9fd3e359ee31def69f5ae899b
    dep_clone limine-protocol \
        https://github.com/limine-bootloader/limine-protocol.git \
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

CINDER_VERSION=0.1
CINDER_URL="https://github.com/z3nnix/cinder/releases/download/v${CINDER_VERSION}/cinder"
CINDER_BIN=/usr/bin/cinder

do_cinder() {
    if [ -x "$CINDER_BIN" ]; then
        say "cinder already installed at $CINDER_BIN"
        return 0
    fi
    say "fetching cinder compiler v$CINDER_VERSION"
    SUDO=""
    [ "$(id -u)" = 0 ] || SUDO="sudo"
    tmp=$(mktemp)
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "$tmp" "$CINDER_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$tmp" "$CINDER_URL"
    else
        rm -f "$tmp"
        die "need curl or wget to download cinder"
    fi
    $SUDO install -m 0755 "$tmp" "$CINDER_BIN"
    rm -f "$tmp"
    say "installed $CINDER_BIN"
}

FW_TAG=20260916
FW_URL="https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain"
FW_FILES="
rtw88/rtw8723d_fw.bin 9c64e079815a333a3f7b731be7cc0be65206ebc883891659dc8ff5ed36a5e2f5
LICENSES/LICENCE.rtlwifi_firmware.txt a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e
"

do_firmware() {
    mkdir -p firmware
    printf '%s\n' "$FW_FILES" | while read -r path sum; do
        [ -n "$path" ] || continue
        case "$path" in
            LICENSES/*) dst="firmware/$(basename "$path")" ;;
            *)          dst="firmware/$path" ;;
        esac
        if [ -f "$dst" ] && [ "$(sha256sum "$dst" | awk '{print $1}')" = "$sum" ]; then
            continue
        fi
        mkdir -p "$(dirname "$dst")"
        say "fetching linux-firmware $FW_TAG: $path"
        curl -fL --retry 3 -o "$dst.part" "$FW_URL/$path?h=$FW_TAG"
        got=$(sha256sum "$dst.part" | awk '{print $1}')
        [ "$got" = "$sum" ] || { rm -f "$dst.part"; die "$path: checksum mismatch ($got)"; }
        mv "$dst.part" "$dst"
    done
}

[ $# -eq 1 ] || die "usage: bootstrap.sh {deps|limine|cinder|firmware}"
case "$1" in
    deps)     do_deps ;;
    limine)   do_limine ;;
    cinder)   do_cinder ;;
    firmware) do_firmware ;;
    *)      die "unknown step: $1" ;;
esac
