#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

RFS=rootfs
TAR=initramfs.tar
INIT_ELF=usr/apps/init.elf
INSTALLER_ELF=usr/installer/cervus-installer.elf
SYSROOT=usr/sysroot
WALLPAPER=wallpapers/cervus1280x720.png
VERSION=v0.0.2

green() { printf '\033[92m[initramfs]\033[0m %s\n' "$*"; }
red()   { printf '\033[91m[initramfs] %s\033[0m\n' "$*" >&2; }

rm -rf "$RFS"
mkdir -p "$RFS"/bin "$RFS"/dev "$RFS"/etc "$RFS"/home "$RFS"/tmp "$RFS"/proc "$RFS"/apps

printf 'root:x:0:0:root:/root:/bin/sh\n' > "$RFS/etc/passwd"
printf 'cervus'                          > "$RFS/etc/hostname"
printf '/bin/csh\n'                      > "$RFS/etc/shell"

cat > "$RFS/etc/motd" <<EOF

    \$\$\$\$\$\$\\
   \$\$  __\$\$\\
   \$\$ /  \\__| \$\$\$\$\$\$\\   \$\$\$\$\$\$\\ \$\$\\    \$\$\\ \$\$\\   \$\$\\  \$\$\$\$\$\$\$\\
   \$\$ |      \$\$  __\$\$\\ \$\$  __\$\$\\\\\$\$\\  \$\$  |\$\$ |  \$\$ |\$\$  _____|
   \$\$ |      \$\$\$\$\$\$\$\$ |\$\$ |  \\__|\\\$\$\\\$\$  / \$\$ |  \$\$ |\\\$\$\$\$\$\$\\
   \$\$ |  \$\$\\ \$\$   ____|\$\$ |       \\\$\$\$  /  \$\$ |  \$\$ | \\____\$\$\\
   \\\$\$\$\$\$\$  |\\\$\$\$\$\$\$\$\\ \$\$ |        \\\$  /   \\\$\$\$\$\$\$  |\$\$\$\$\$\$\$  |
    \\______/  \\______||\\__|         \\_/     \\______/ \\_______/

 Cervus OS $VERSION (Alpha release)

 Type 'help' to see available commands.

EOF

# init -> /bin/init (required)
if [ -f "$INIT_ELF" ]; then
    cp "$INIT_ELF" "$RFS/bin/init"
    green "init.elf -> /bin/init"
else
    red "init.elf not found - boot will drop to nothing!"
fi

# /bin programs
for elf in usr/bin/*.elf; do
    [ -e "$elf" ] || continue
    cp "$elf" "$RFS/bin/$(basename "$elf" .elf)"
done
green "copied /bin programs"

# /apps programs (excluding init)
for elf in usr/apps/*.elf; do
    [ -e "$elf" ] || continue
    name=$(basename "$elf" .elf)
    [ "$name" = init ] && continue
    cp "$elf" "$RFS/apps/$name"
done
green "copied /apps programs"

# installer
if [ -f "$INSTALLER_ELF" ]; then
    cp "$INSTALLER_ELF" "$RFS/bin/cervus-installer"
    green "cervus-installer -> /bin/cervus-installer"
fi

# sysroot (tcc, libs, headers) -> /usr
if [ -d "$SYSROOT/usr" ]; then
    mkdir -p "$RFS/usr"
    cp -r "$SYSROOT"/usr/. "$RFS/usr/"
    green "sysroot installed into /usr"
else
    red "$SYSROOT/usr not found - skipping sysroot"
fi

cat > "$RFS/home/readme.txt" <<EOF
Cervus OS v0.0.2
================

This is Cervus - an x86_64 OS written in C.
Bootloader: Limine | Filesystem: ext2

Source: https://github.com/VeoQeo/Cervus
EOF

cat > "$RFS/home/welcome.txt" <<EOF
Welcome to Cervus Shell!

Tips:
  - Use arrow keys to move cursor within a command
  - Use Up/Down to browse command history (saved in ~/.history)
  - Press Tab to autocomplete commands and paths
  - Binaries are in /bin and /apps
EOF

# boot files
mkdir -p "$RFS/boot"
copy_boot() {  # $1=src $2=dst $3=required
    if [ -f "$1" ]; then
        cp "$1" "$2"; green "$1 -> $2"
    elif [ "$3" = required ]; then
        red "missing required boot file: $1"; exit 1
    else
        printf '\033[93m[initramfs] skip (not built): %s\033[0m\n' "$1"
    fi
}
copy_boot bin/kernel                 "$RFS/boot/kernel"                required
copy_boot "$INIT_ELF"                "$RFS/boot/shell.elf"             required
copy_boot limine/limine-bios.sys     "$RFS/boot/limine-bios.sys"       optional
copy_boot limine/limine-bios-hdd.bin "$RFS/boot/limine-bios-hdd.bin"   optional
copy_boot limine/BOOTX64.EFI         "$RFS/boot/BOOTX64.EFI"           optional
copy_boot limine/BOOTIA32.EFI        "$RFS/boot/BOOTIA32.EFI"          optional
copy_boot "$WALLPAPER"               "$RFS/boot/wallpaper.png"         optional

green "packing $TAR"
tar --format=ustar -cf "$TAR" -C "$RFS" .
green "$TAR built"
