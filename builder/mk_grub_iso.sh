#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

IMAGE=Cervus
VERSION=v0.0.2
INIT_ELF=usr/apps/init.elf

green() { printf '\033[92m[grub-iso]\033[0m %s\n' "$*"; }
red()   { printf '\033[91m[grub-iso] %s\033[0m\n' "$*" >&2; }

[ -f bin/kernel ] || { red "bin/kernel not found"; exit 1; }
command -v grub-mkrescue >/dev/null 2>&1 || command -v grub2-mkrescue >/dev/null 2>&1 || {
    red "grub-mkrescue not found (install grub + xorriso + mtools)"; exit 1;
}
MKRESCUE=$(command -v grub-mkrescue 2>/dev/null || command -v grub2-mkrescue)

rm -rf grub_root
mkdir -p grub_root/boot/grub grub_root/boot/grub/fonts demo_iso
cp bin/kernel grub_root/boot/kernel

have_font=false
for pf2 in /usr/share/grub/unicode.pf2 /usr/lib/grub/unicode.pf2 \
           /boot/grub/fonts/unicode.pf2 /usr/share/grub2/unicode.pf2; do
    if [ -f "$pf2" ]; then
        cp "$pf2" grub_root/boot/grub/fonts/unicode.pf2
        have_font=true
        green "gfxterm font: $pf2"
        break
    fi
done
[ "$have_font" = true ] || red "unicode.pf2 not found - gfxterm may render garbled"

has_elf=false
if [ -f "$INIT_ELF" ]; then
    cp "$INIT_ELF" grub_root/boot/shell.elf; has_elf=true
    green "init.elf -> boot/shell.elf"
else
    red "init.elf not found - boot will fail!"
fi

has_initramfs=false
if [ -f initramfs.tar ]; then
    cp initramfs.tar grub_root/boot/initramfs.tar; has_initramfs=true
    green "initramfs.tar -> boot/initramfs.tar"
fi

{
    printf 'set timeout=3\n'
    printf 'set default=0\n\n'
    printf 'insmod all_video\n\n'
    printf 'menuentry "%s %s (multiboot2)" {\n' "$IMAGE" "$VERSION"
    printf '    insmod all_video\n'
    printf '    set gfxpayload=%s\n' "${CERVUS_GRUB_GFXMODE:-1280x1024}"
    printf '    multiboot2 /boot/kernel\n'
    [ "$has_elf" = true ]       && printf '    module2 /boot/shell.elf init\n'
    [ "$has_initramfs" = true ] && printf '    module2 /boot/initramfs.tar initramfs\n'
    printf '    boot\n'
    printf '}\n'
} > grub_root/boot/grub/grub.cfg

ts=$(date +%Y%m%d_%H%M%S)
iso="demo_iso/${IMAGE}-grub.${VERSION}.${ts}.iso"

"$MKRESCUE" -o "$iso" grub_root >/dev/null 2>&1

rm -f "demo_iso/${IMAGE}-grub.latest.iso"
cp -f "$iso" "demo_iso/${IMAGE}-grub.latest.iso"
rm -rf grub_root
green "GRUB ISO ready: $iso"
