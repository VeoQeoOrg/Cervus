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
mkdir -p "$RFS"/bin "$RFS"/dev "$RFS"/etc "$RFS"/etc/skel "$RFS"/home "$RFS"/tmp "$RFS"/proc "$RFS"/apps "$RFS"/root "$RFS"/lib

printf 'root:x:0:0:root:/root:/bin/csh\n' > "$RFS/etc/passwd"
chmod 0644 "$RFS/etc/passwd"
: > "$RFS/etc/sudoers"
chmod 0600 "$RFS/etc/sudoers"
: > "$RFS/etc/shadow"
chmod 0600 "$RFS/etc/shadow"
chmod 1777 "$RFS/tmp"
chmod 0700 "$RFS/root"
printf 'cervus'                          > "$RFS/etc/hostname"
printf '/bin/csh\n'                      > "$RFS/etc/shell"
printf 'pool.ntp.org\n'                  > "$RFS/etc/ntp"

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

if [ -f "$INIT_ELF" ]; then
    cp "$INIT_ELF" "$RFS/bin/init"
    green "init.elf -> /bin/init"
else
    red "init.elf not found - boot will drop to nothing!"
fi

for elf in usr/bin/*.elf; do
    [ -e "$elf" ] || continue
    cp "$elf" "$RFS/bin/$(basename "$elf" .elf)"
done
[ -e "$RFS/bin/test" ] && cp "$RFS/bin/test" "$RFS/bin/["

if [ -f usr/ldso/ld-cervus.elf ]; then
    cp usr/ldso/ld-cervus.elf "$RFS/lib/ld-cervus.elf"
    green "dynamic loader installed"
fi
for so in usr/lib/shared/*.so; do
    [ -e "$so" ] || continue
    cp "$so" "$RFS/lib/$(basename "$so")"
    green "shared library $(basename "$so") installed"
done
green "copied /bin programs"

for elf in usr/apps/*.elf; do
    [ -e "$elf" ] || continue
    name=$(basename "$elf" .elf)
    [ "$name" = init ] && continue
    cp "$elf" "$RFS/apps/$name"
done
green "copied /apps programs"

if [ -f "$INSTALLER_ELF" ]; then
    cp "$INSTALLER_ELF" "$RFS/bin/cervus-installer"
    green "cervus-installer -> /bin/cervus-installer"
fi

if [ -d "$SYSROOT/usr" ]; then
    mkdir -p "$RFS/usr"
    cp -r "$SYSROOT"/usr/. "$RFS/usr/"
    green "sysroot installed into /usr"
else
    red "$SYSROOT/usr not found - skipping sysroot"
fi

for d in "$SYSROOT"/etc/*/; do
    [ -d "$d" ] || continue
    case "$d" in *ssl/) continue ;; esac
    name=$(basename "$d")
    mkdir -p "$RFS/etc/$name"
    cp -r "$d". "$RFS/etc/$name/"
    green "sysroot etc/$name installed"
done

if [ -f "$SYSROOT/etc/ssl/certs/ca-certificates.crt" ]; then
    mkdir -p "$RFS/etc/ssl/certs"
    cp "$SYSROOT/etc/ssl/certs/ca-certificates.crt" "$RFS/etc/ssl/certs/ca-certificates.crt"
    green "CA bundle installed into /etc/ssl/certs"
fi

cat > "$RFS/etc/skel/welcome.txt" <<EOF
Welcome to Cervus!

Getting around
  help              what the shell itself can do
  man <name>        manual page for a command
  apropos <word>    find a command by what it does
  ls /bin /apps     everything installed

Editing the terminal
  theme             colour schemes; 'theme cervus' is easier on the eyes
  setfont <file>    console font, PSF or TrueType, from /usr/share/fonts
  mode              screen resolution, where the adapter allows it

Line editing
  Left/Right        move within the command
  Up/Down           history, kept in ~/.history
  Tab               complete commands and paths
  Ctrl-C            interrupt what is running
  Ctrl-Alt-F1..F12  switch between virtual terminals; F2 is the debug log

Files and disks
  cfm               file manager with a preview pane
  neo               text editor
  lsblk / mount     what is attached, and how to reach it

Cervus OS v0.0.2 - an x86_64 operating system written from scratch in C.
Source: https://github.com/VeoQeoOrg/Cervus
EOF
chmod 0644 "$RFS/etc/skel/welcome.txt"

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
copy_boot builder/grub-bios.img      "$RFS/boot/grub-bios.img"         optional
copy_boot limine/BOOTX64.EFI         "$RFS/boot/BOOTX64.EFI"           optional
copy_boot limine/BOOTIA32.EFI        "$RFS/boot/BOOTIA32.EFI"          optional
copy_boot "$WALLPAPER"               "$RFS/boot/wallpaper.png"         optional

green "packing $TAR"
tar --format=ustar -cf "$TAR" -C "$RFS" .
green "$TAR built"
