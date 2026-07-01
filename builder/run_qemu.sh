#!/bin/sh
# Prepare disk image(s) and launch QEMU. Ported from build.c's
# prepare_disks() / launch_qemu() / launch_installed() / build_data_iso().
#
# Flags (combine freely):
#   --uefi            boot via UEFI/OVMF          (default: BIOS)
#   --disk=MODE       ide | ahci | nvme | all | none   (default: ide)
#   --live            no disk, boot ISO live      (same as --disk=none)
#   --fresh           recreate empty disk image(s) before boot
#   --installed       boot existing disk only, no ISO (simulate real HW)
#
# The ISO is expected to already exist at demo_iso/Cervus.latest.iso
# (nb builds it before calling this, except for --installed).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

QEMUFLAGS="-m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
ISO="demo_iso/Cervus.latest.iso"

UEFI=false
DISK=ide
FRESH=false
INSTALLED=false

for a in "$@"; do
    case "$a" in
        --uefi)      UEFI=true ;;
        --live)      DISK=none ;;
        --fresh|--reset-disk) FRESH=true ;;
        --installed) INSTALLED=true ;;
        --disk=ide|--disk=ahci|--disk=nvme|--disk=all|--disk=none)
            DISK=${a#--disk=} ;;
        *) echo "run: unknown option '$a'" >&2; exit 1 ;;
    esac
done

green() { printf '\033[92m%s\033[0m\n' "$*"; }
yellow(){ printf '\033[93m%s\033[0m\n' "$*"; }
red()   { printf '\033[91m%s\033[0m\n' "$*" >&2; }

# --- OVMF discovery (UEFI) --------------------------------------------------
find_ovmf() {
    for p in \
        /usr/share/edk2/x64/OVMF.4m.fd \
        /usr/share/edk2/x64/OVMF_CODE.4m.fd \
        /usr/share/edk2/ovmf/OVMF.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/share/ovmf/x64/OVMF.fd \
        /usr/share/ovmf/x64/OVMF_CODE.fd \
        /usr/share/ovmf/OVMF.fd \
        /usr/share/OVMF/OVMF.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/qemu/OVMF.fd; do
        [ -f "$p" ] && { printf '%s' "$p"; return 0; }
    done
    return 1
}

BIOS=""
if $UEFI; then
    ovmf=$(find_ovmf) || { red "OVMF not found (sudo apt install ovmf)"; exit 1; }
    BIOS=" -bios $ovmf"
    green "UEFI/OVMF: $ovmf"
fi

mk_disk() {  # $1=file
    if $FRESH && [ -f "$1" ]; then yellow "[disk] removing $1"; rm -f "$1"; fi
    if [ ! -f "$1" ]; then
        green "Creating $1 (256MB)..."
        dd if=/dev/zero of="$1" bs=1M count=256 status=none
    fi
}

# --- sample data CD for --disk=all (ISO9660/ATAPI test) --------------------
build_data_iso() {
    [ -f cervus_data.iso ] && return 0
    root=data_iso_root
    rm -rf "$root"; mkdir -p "$root/docs"
    printf 'Hello from a CD-ROM!\n' > "$root/hello.txt"
    printf 'Nested dir file.\n'     > "$root/docs/hello.txt"
    printf 'Cervus sample data CD\nmount /dev/sdb /mnt/cdrom\n' > "$root/readme.txt"
    green "Building cervus_data.iso..."
    xorriso -as mkisofs -r -J -V CERVUS_DATA -o cervus_data.iso "$root" >/dev/null 2>&1
    rm -rf "$root"
}

# --- installed: boot the disk directly, no ISO -----------------------------
if $INSTALLED; then
    [ -f cervus_disk.img ] || { red "cervus_disk.img not found. Run './nb run' first."; exit 1; }
    green "Booting installed disk (no ISO)..."
    if [ "$DISK" = nvme ]; then
        exec qemu-system-x86_64 -machine q35$BIOS \
            -drive id=nvm0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
            -device nvme,serial=CRV001,drive=nvm0 -serial stdio $QEMUFLAGS
    else
        exec qemu-system-x86_64 -machine pc$BIOS \
            -drive file=cervus_disk.img,format=raw,if=ide -serial stdio $QEMUFLAGS
    fi
fi

[ -e "$ISO" ] || { red "no ISO at $ISO"; exit 1; }

# --- prepare disks + launch per mode ---------------------------------------
case "$DISK" in
none)
    green "Starting QEMU (live, no disk)..."
    exec qemu-system-x86_64 -machine pc$BIOS -cdrom "$ISO" -boot d \
        -serial stdio $QEMUFLAGS
    ;;
all)
    mk_disk cervus_ata.img; mk_disk cervus_sata.img; mk_disk cervus_nvme.img
    build_data_iso
    green "Starting QEMU (ATA + SATA + NVMe + CDROM)..."
    exec qemu-system-x86_64 -machine q35$BIOS \
        -drive id=ata0,file=cervus_ata.img,format=raw,if=none \
        -device ide-hd,bus=ide.0,unit=0,drive=ata0,bootindex=2 \
        -drive id=cd0,file="$ISO",format=raw,if=none,media=cdrom \
        -device ide-cd,bus=ide.1,unit=0,drive=cd0,bootindex=10 \
        -device ich9-ahci,id=ahci \
        -drive id=sata0,file=cervus_sata.img,format=raw,if=none,file.locking=off \
        -device ide-hd,bus=ahci.0,drive=sata0,bootindex=3 \
        -drive id=cd_data,file=cervus_data.iso,format=raw,if=none,media=cdrom,file.locking=off \
        -device ide-cd,bus=ahci.1,drive=cd_data \
        -drive id=nvm0,file=cervus_nvme.img,format=raw,if=none,file.locking=off \
        -device nvme,serial=CRV001,drive=nvm0,bootindex=4 \
        -device qemu-xhci,id=xhci -boot menu=on,splash-time=2000 \
        -serial stdio $QEMUFLAGS
    ;;
ahci)
    mk_disk cervus_disk.img
    green "Starting QEMU (AHCI/SATA, q35)..."
    exec qemu-system-x86_64 -machine q35$BIOS -cdrom "$ISO" -boot d \
        -drive id=hd0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
        -device ich9-ahci,id=ahci -device ide-hd,bus=ahci.0,drive=hd0 \
        -serial stdio $QEMUFLAGS
    ;;
nvme)
    mk_disk cervus_disk.img
    green "Starting QEMU (NVMe, q35)..."
    exec qemu-system-x86_64 -machine q35$BIOS -cdrom "$ISO" -boot d \
        -drive id=nvm0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
        -device nvme,serial=CRV001,drive=nvm0 \
        -serial stdio $QEMUFLAGS
    ;;
ide|*)
    mk_disk cervus_disk.img
    green "Starting QEMU (BIOS, IDE)..."
    exec qemu-system-x86_64 -machine pc$BIOS -cdrom "$ISO" -boot d \
        -serial stdio $QEMUFLAGS \
        -drive file=cervus_disk.img,format=raw,if=ide,index=0,media=disk
    ;;
esac
