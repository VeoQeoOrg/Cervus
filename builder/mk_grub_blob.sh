#!/bin/sh
# Generate builder/grub-bios.img: a self-booting GRUB (i386-pc) boot blob
# (boot.img in the MBR + core.img in the post-MBR embedding gap) that the
# Cervus installer writes verbatim to a target disk. core.img has its modules
# embedded and a prefix of (hd0,msdos1)/boot/grub, matching the installer's
# layout (ESP = MBR partition 1, GRUB config + kernel under /boot there).
#
# Needs root for losetup/mount/grub-install; run once when the host GRUB
# changes:  sudo builder/mk_grub_blob.sh   (or with passwordless sudo rules)
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

SUDO=""
[ "$(id -u)" = "0" ] || SUDO="sudo -n"
OUT="builder/grub-bios.img"
WORK=$(mktemp -d)
IMG="$WORK/ref.img"
MNT="$WORK/mnt"
OFF=$((2048 * 512))
LOOP=""

cleanup() {
    [ -n "$LOOP" ] && { $SUDO /usr/bin/umount "$MNT" 2>/dev/null || true; $SUDO /usr/bin/losetup -d "$LOOP" 2>/dev/null || true; }
    rm -rf "$WORK"
}
trap cleanup EXIT

green() { printf '\033[92m[grub-blob]\033[0m %s\n' "$*"; }

truncate -s 128M "$IMG"
python3 - "$IMG" <<'PY'
import sys, struct, os
img = sys.argv[1]
nsect = (os.path.getsize(img) // 512) - 2048
mbr = bytearray(512)
mbr[446:462] = struct.pack('<B3sB3sII', 0x80, b'\xfe\xff\xff', 0x0C, b'\xfe\xff\xff', 2048, nsect)
mbr[510] = 0x55; mbr[511] = 0xAA
with open(img, 'r+b') as f:
    f.write(mbr)
PY

mformat -i "$IMG@@$OFF" -F -v CERVUS_ESP ::
mmd -i "$IMG@@$OFF" ::/boot ::/boot/grub
printf 'placeholder\n' > "$WORK/grub.cfg"
mcopy -i "$IMG@@$OFF" "$WORK/grub.cfg" ::/boot/grub/grub.cfg

LOOP=$($SUDO /usr/bin/losetup -fP --show "$IMG")
green "loop: $LOOP"
mkdir -p "$MNT"
$SUDO /usr/bin/mount "${LOOP}p1" "$MNT"
$SUDO /usr/bin/grub-install --target=i386-pc --boot-directory="$MNT/boot" \
    --modules="part_msdos fat multiboot2 configfile normal search search_fs_file" "$LOOP"
$SUDO /usr/bin/umount "$MNT"
$SUDO /usr/bin/losetup -d "$LOOP"; LOOP=""

# Extract MBR + core.img (up to the last non-empty gap sector before the partition)
python3 - "$IMG" "$OUT" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read(2048 * 512)
last = 0
for s in range(1, 2048):
    if any(d[s*512:(s+1)*512]):
        last = s
open(sys.argv[2], 'wb').write(d[:(last + 1) * 512])
print(f"grub-blob: {last+1} sectors ({(last+1)*512} bytes) -> {sys.argv[2]}")
PY
green "wrote $OUT"
