#!/bin/sh
set -eu

green() { printf '\033[92m[deps]\033[0m %s\n' "$*"; }
yellow(){ printf '\033[93m[deps]\033[0m %s\n' "$*"; }
red()   { printf '\033[91m[deps] %s\033[0m\n' "$*" >&2; }

distro_id=""
[ -r /etc/os-release ] && distro_id=$(. /etc/os-release 2>/dev/null; printf '%s %s' "${ID:-}" "${ID_LIKE:-}")

SUDO=""
if [ "$(id -u)" != "0" ]; then
    if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
    elif command -v doas >/dev/null 2>&1; then SUDO="doas"
    else red "need root: run as root or install sudo/doas"; exit 1; fi
fi

run() { green "running: $SUDO $*"; $SUDO "$@"; }

if command -v pacman >/dev/null 2>&1; then
    green "Arch (pacman) detected${distro_id:+: $distro_id}"
    run pacman -S --needed --noconfirm \
        base-devel nasm ninja xorriso mtools grub qemu-system-x86 \
        git curl wget
elif command -v apt-get >/dev/null 2>&1; then
    green "Debian/Ubuntu (apt) detected${distro_id:+: $distro_id}"
    run apt-get update
    run apt-get install -y \
        build-essential nasm ninja-build xorriso mtools \
        grub-common grub-pc-bin qemu-system-x86 \
        git curl wget
elif command -v dnf >/dev/null 2>&1; then
    green "Fedora/RHEL (dnf) detected${distro_id:+: $distro_id}"
    run dnf install -y \
        gcc binutils make nasm ninja-build xorriso mtools \
        grub2-tools grub2-tools-extra qemu-system-x86 \
        git curl wget
elif command -v zypper >/dev/null 2>&1; then
    green "openSUSE (zypper) detected${distro_id:+: $distro_id}"
    run zypper install -y \
        gcc binutils make nasm ninja xorriso mtools grub2 qemu-x86 \
        git curl wget
elif command -v apk >/dev/null 2>&1; then
    green "Alpine (apk) detected${distro_id:+: $distro_id}"
    run apk add \
        build-base nasm ninja xorriso mtools grub grub-bios \
        qemu-system-x86_64 git curl wget
elif command -v xbps-install >/dev/null 2>&1; then
    green "Void (xbps) detected${distro_id:+: $distro_id}"
    run xbps-install -Sy \
        gcc binutils make nasm ninja xorriso mtools grub qemu \
        git curl wget
else
    red "no supported package manager found (pacman/apt/dnf/zypper/apk/xbps)"
    yellow "install manually: gcc binutils make nasm ninja xorriso mtools grub(-mkrescue) qemu-system-x86_64 git curl wget"
    exit 1
fi

green "done. now run ./nb to build, or ./nb run to boot in QEMU."
