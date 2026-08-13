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
#   --net             attach an e1000 NIC with user-mode networking (internet via host NAT)
#   --net=ne2k        attach a NE2000 (ne2k_pci) NIC instead of e1000
#
# The ISO is expected to already exist at demo_iso/Cervus.latest.iso
# (nb builds it before calling this, except for --installed).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

QEMUFLAGS="-m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
ISO="demo_iso/Cervus.latest.iso"
RUNLOG="cervus-dev.log"
SERIAL="-chardev stdio,id=cervlog,logfile=$RUNLOG,signal=off -serial chardev:cervlog"

UEFI=false
DISK=ide
FRESH=false
INSTALLED=false
NET=""
RES=""
SND_ON=true
SND_BACKEND=auto

for a in "$@"; do
    case "$a" in
        --uefi)      UEFI=true ;;
        --live)      DISK=none ;;
        --fresh|--reset-disk) FRESH=true ;;
        --installed) INSTALLED=true ;;
        --net)         NET=" -netdev user,id=net0 -device e1000,netdev=net0" ;;
        --net=ne2k)    NET=" -netdev user,id=net0 -device ne2k_pci,netdev=net0" ;;
        --net=rtl8139) NET=" -netdev user,id=net0 -device rtl8139,netdev=net0" ;;
        --net=virtio)  NET=" -netdev user,id=net0 -device virtio-net-pci,netdev=net0" ;;
        --no-sound)  SND_ON=false ;;
        --sound)     SND_ON=true ;;
        --sound=*)   SND_ON=true; SND_BACKEND=${a#--sound=} ;;
        --grub)      ISO="demo_iso/Cervus-grub.latest.iso" ;;
        --limine)    ISO="demo_iso/Cervus.latest.iso" ;;
        --res=*)     RES=${a#--res=} ;;
        --disk=ide|--disk=ahci|--disk=nvme|--disk=all|--disk=none)
            DISK=${a#--disk=} ;;
        *) echo "run: unknown option '$a'" >&2; exit 1 ;;
    esac
done

detect_audiodev() {
    if [ -S "/run/user/$(id -u)/pulse/native" ] || [ -n "${PULSE_SERVER:-}" ]; then echo pa; return; fi
    if [ -e /dev/snd/timer ] || ls /dev/snd/pcmC* >/dev/null 2>&1; then echo alsa; return; fi
    echo none
}

SOUND=""
if $SND_ON; then
    dev=ac97
    b=$SND_BACKEND
    case "$SND_BACKEND" in
        hda)   dev=hda; b=auto ;;
        hda:*) dev=hda; b=${SND_BACKEND#hda:} ;;
        ac97:*) b=${SND_BACKEND#ac97:} ;;
    esac
    [ "$b" = auto ] && b=$(detect_audiodev)

    if [ "$b" = wav ]; then
        AUDIODEV="-audiodev wav,id=snd0,path=cervus-audio.wav"
    else
        AUDIODEV="-audiodev $b,id=snd0"
    fi
    if [ "$dev" = hda ]; then
        SOUND=" $AUDIODEV -device intel-hda -device hda-duplex,audiodev=snd0"
    else
        SOUND=" $AUDIODEV -device AC97,audiodev=snd0"
    fi
fi

QEMUFLAGS="$QEMUFLAGS$NET$SOUND"

if [ -n "$RES" ]; then
    case "$RES" in
        *x*) RW=${RES%x*}; RH=${RES#*x} ;;
        *)   echo "run: --res expects WIDTHxHEIGHT (e.g. --res=1920x1080)" >&2; exit 1 ;;
    esac
    QEMUFLAGS="$QEMUFLAGS -vga none -device VGA,edid=on,xres=$RW,yres=$RH"
fi

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

green "Serial log -> $ROOT/$RUNLOG"

# --- installed: boot the disk directly, no ISO -----------------------------
if $INSTALLED; then
    [ -f cervus_disk.img ] || { red "cervus_disk.img not found. Run './nb run' first."; exit 1; }
    green "Booting installed disk (no ISO)..."
    if [ "$DISK" = nvme ]; then
        exec qemu-system-x86_64 -machine q35$BIOS \
            -drive id=nvm0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
            -device nvme,serial=CRV001,drive=nvm0 $SERIAL $QEMUFLAGS
    else
        exec qemu-system-x86_64 -machine pc$BIOS \
            -drive file=cervus_disk.img,format=raw,if=ide $SERIAL $QEMUFLAGS
    fi
fi

[ -e "$ISO" ] || { red "no ISO at $ISO"; exit 1; }

# --- prepare disks + launch per mode ---------------------------------------
case "$DISK" in
none)
    green "Starting QEMU (live, no disk)..."
    exec qemu-system-x86_64 -machine pc$BIOS -cdrom "$ISO" -boot d \
        $SERIAL $QEMUFLAGS
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
        $SERIAL $QEMUFLAGS
    ;;
ahci)
    mk_disk cervus_disk.img
    green "Starting QEMU (AHCI/SATA, q35)..."
    exec qemu-system-x86_64 -machine q35$BIOS -cdrom "$ISO" -boot d \
        -drive id=hd0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
        -device ich9-ahci,id=ahci -device ide-hd,bus=ahci.0,drive=hd0 \
        $SERIAL $QEMUFLAGS
    ;;
nvme)
    mk_disk cervus_disk.img
    green "Starting QEMU (NVMe, q35)..."
    exec qemu-system-x86_64 -machine q35$BIOS -cdrom "$ISO" -boot d \
        -drive id=nvm0,file=cervus_disk.img,format=raw,if=none,file.locking=off \
        -device nvme,serial=CRV001,drive=nvm0 \
        $SERIAL $QEMUFLAGS
    ;;
ide|*)
    mk_disk cervus_disk.img
    green "Starting QEMU (BIOS, IDE)..."
    exec qemu-system-x86_64 -machine pc$BIOS -cdrom "$ISO" -boot d \
        $SERIAL $QEMUFLAGS \
        -drive file=cervus_disk.img,format=raw,if=ide,index=0,media=disk
    ;;
esac
