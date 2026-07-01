#!/bin/sh
# Build a bootable ISO with Limine. Ported from build.c:create_iso().
# Produces demo_iso/Cervus.<version>.<timestamp>.iso and a
# demo_iso/Cervus.latest.iso symlink.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

IMAGE=Cervus
VERSION=v0.0.2
INIT_ELF=usr/apps/init.elf
WALLPAPER=wallpapers/cervus1280x720.png
WALLPAPER_DST="boot():/boot/wallpapers/cervus.png"

green() { printf '\033[92m[iso]\033[0m %s\n' "$*"; }
red()   { printf '\033[91m[iso] %s\033[0m\n' "$*" >&2; }

[ -f bin/kernel ] || { red "bin/kernel not found"; exit 1; }
[ -f limine/limine ] || { red "limine not built (run builder/bootstrap.sh limine)"; exit 1; }

rm -rf iso_root
mkdir -p iso_root/boot/limine iso_root/boot/wallpapers iso_root/EFI/BOOT demo_iso

[ -f "$WALLPAPER" ] && cp "$WALLPAPER" iso_root/boot/wallpapers/cervus.png
cp bin/kernel iso_root/boot/kernel

has_elf=false
if [ -f "$INIT_ELF" ]; then
    cp "$INIT_ELF" iso_root/boot/shell.elf; has_elf=true
    green "init.elf -> boot/shell.elf"
else
    red "init.elf not found - boot will fail!"
fi

has_initramfs=false
if [ -f initramfs.tar ]; then
    cp initramfs.tar iso_root/boot/initramfs.tar; has_initramfs=true
    green "initramfs.tar -> boot/initramfs.tar"
fi

# --- limine.conf -----------------------------------------------------------
{
    [ -f "$WALLPAPER" ] && printf 'wallpaper: %s\n' "$WALLPAPER_DST"
    printf 'timeout: 5\n\n'
    printf '/%s %s (Install / Live)\n    protocol: limine\n    path: boot():/boot/kernel\n' \
        "$IMAGE" "$VERSION"
    if [ "$has_elf" = true ]; then
        printf '    module_path: boot():/boot/shell.elf\n    module_cmdline: init\n'
    fi
    if [ "$has_initramfs" = true ]; then
        printf '    module_path: boot():/boot/initramfs.tar\n    module_cmdline: initramfs\n'
    fi
} > limine.conf

cp limine.conf iso_root/boot/limine/
cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin \
    iso_root/boot/limine/
cp limine/BOOTX64.EFI limine/BOOTIA32.EFI iso_root/EFI/BOOT/

ts=$(date +%Y%m%d_%H%M%S)
iso="demo_iso/${IMAGE}.${VERSION}.${ts}.iso"

xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    iso_root -o "$iso"

./limine/limine bios-install "$iso"

ln -sf "$(basename "$iso")" "demo_iso/${IMAGE}.latest.iso"
rm -rf iso_root
green "ISO ready: $iso"
