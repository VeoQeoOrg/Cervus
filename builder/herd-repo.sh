#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

USAGE="Usage: builder/herd-repo.sh <command> [args]
Build and sign a cervus-ports repository.

  keygen DIR              make DIR/herd.seed (private) and DIR/herd.pub
  package NAME VER STAGE OUT
                          turn a staged root STAGE into OUT/NAME-VER-x86_64
                          .tar.gz plus its .manifest
  index OUT SEEDFILE      concatenate every manifest in OUT into OUT/INDEX
                          and sign it as OUT/INDEX.sig

A staged root is a directory that looks like / does: STAGE/usr/bin/nasm
becomes /usr/bin/nasm on the installed system. Give a recipe's make step
DESTDIR=STAGE and it lands there."

SIGN="$ROOT/builder/herd/sign"

build_signer() {
    [ -x "$SIGN" ] && return 0
    cc -O2 -o "$SIGN" "$ROOT/builder/herd/sign.c" \
       "$ROOT/usr/lib/libcervus/crypto/curve25519.c" \
       "$ROOT/usr/lib/libcervus/crypto/sha512.c" \
       -I"$ROOT/usr/sysroot/usr/include"
}

cmd_keygen() {
    dir=${1:?keygen needs a directory}
    mkdir -p "$dir"
    build_signer
    "$SIGN" keygen "$dir/herd.seed" "$dir/herd.pub"
    chmod 600 "$dir/herd.seed"
    echo "private key: $dir/herd.seed   (never publish this)"
    echo "public  key: $dir/herd.pub    (ship as /etc/herd.pub)"
}

cmd_package() {
    name=${1:?}; ver=${2:?}; stage=${3:?}; out=${4:?}
    [ -d "$stage" ] || { echo "herd-repo: no staged root at $stage" >&2; exit 1; }
    mkdir -p "$out"
    tarball="$out/$name-$ver-x86_64.tar.gz"

    ( cd "$stage" && tar -czf - . ) > "$tarball"

    size=$(wc -c < "$tarball" | tr -d ' ')
    sha=$(sha256sum "$tarball" | awk '{print $1}')

    manifest="$out/$name-$ver-x86_64.manifest"
    {
        echo "name: $name"
        echo "version: $ver"
        echo "arch: x86_64"
        echo "size: $size"
        echo "sha256: $sha"
        echo "file: $name-$ver-x86_64.tar.gz"
        [ -n "${DEPENDS:-}" ] && echo "depends: $DEPENDS"
        [ -n "${SUMMARY:-}" ] && echo "summary: $SUMMARY"
        [ -n "${LICENSE:-}" ] && echo "license: $LICENSE"
    } > "$manifest"

    echo "$tarball  ($size bytes)"
    echo "$manifest"
}

cmd_index() {
    out=${1:?}; seed=${2:?}
    [ -f "$seed" ] || { echo "herd-repo: no key at $seed (run keygen)" >&2; exit 1; }
    build_signer

    : > "$out/INDEX"
    first=1
    for m in "$out"/*.manifest; do
        [ -e "$m" ] || continue
        [ $first -eq 1 ] || echo "" >> "$out/INDEX"
        cat "$m" >> "$out/INDEX"
        first=0
    done

    "$SIGN" sign "$seed" "$out/INDEX" "$out/INDEX.sig"
    n=$(grep -c '^name:' "$out/INDEX" || true)
    echo "$out/INDEX      ($n package(s))"
    echo "$out/INDEX.sig  signed"
}

[ $# -ge 1 ] || { echo "$USAGE"; exit 2; }
c=$1; shift
case "$c" in
    keygen)  cmd_keygen "$@" ;;
    package) cmd_package "$@" ;;
    index)   cmd_index "$@" ;;
    -h|--help) echo "$USAGE" ;;
    *) echo "herd-repo: unknown command '$c'" >&2; echo "$USAGE" >&2; exit 2 ;;
esac
