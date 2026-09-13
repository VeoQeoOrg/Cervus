#!/bin/sh
# Boot Cervus across a matrix of emulated machines and report which
# configurations reach userspace cleanly, how fast, and which fault.
#
#   builder/hwtest.sh [--tcg] [--jobs N] [--timeout S] [--workload] [pattern]
#
# With no pattern every profile runs; a pattern (a shell glob) restricts
# the run to matching profile names. Results land in hwtest-out/.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

ACCEL=tcg
JOBS=1
TIMEOUT=25   # per-machine boot budget in seconds
WORKLOAD=0
PATTERN='*'


while [ $# -gt 0 ]; do
  case "$1" in
    --tcg) ACCEL=tcg ;;
    --kvm) ACCEL=kvm ;;
    --jobs) JOBS=$2; shift ;;
    --jobs=*) JOBS=${1#*=} ;;
    --timeout) TIMEOUT=$2; shift ;;
    --timeout=*) TIMEOUT=${1#*=} ;;
    --workload) WORKLOAD=1 ;;
    -*) echo "hwtest: unknown option $1" >&2; exit 2 ;;
    *) PATTERN=$1 ;;
  esac
  shift
done

ISO="$ROOT/demo_iso/Cervus.latest.iso"
[ -e "$ISO" ] || { echo "hwtest: no ISO at $ISO (run ./nb first)" >&2; exit 1; }
ISO=$(readlink -f "$ISO")

OUT="$ROOT/hwtest-out"
rm -rf "$OUT"; mkdir -p "$OUT"
REPORT="$OUT/report.txt"
BLANK="$OUT/blank.img"
qemu-img create -q -f raw "$BLANK" 256M 2>/dev/null || dd if=/dev/zero of="$BLANK" bs=1M count=256 status=none

ACCELARG=
[ "$ACCEL" = kvm ] && ACCELARG=",accel=kvm"


# ---- profile matrix -------------------------------------------------------
# Each line: NAME|QEMU-ARGS   (ISO, serial, monitor, display added per run)
profiles() {
  # Baselines per CPU generation, q35 + pc, a laptop-ish and a desktop-ish shape.
  for cpu in qemu64 kvm64 core2duo Conroe Penryn Nehalem Westmere \
             SandyBridge IvyBridge Haswell Haswell-noTSX Broadwell \
             Skylake-Client Skylake-Server Cascadelake-Server Cooperlake \
             Icelake-Server SapphireRapids \
             Opteron_G1 Opteron_G2 Opteron_G3 Opteron_G4 Opteron_G5 \
             phenom EPYC EPYC-Rome EPYC-Milan; do
    echo "q35-$cpu-2c|-machine q35$ACCELARG -cpu $cpu -smp 2 -m 2048 -device e1000,netdev=n0 -netdev user,id=n0 -device ich9-ahci,id=ahci -drive id=hd,file=$BLANK,format=raw,if=none -device ide-hd,bus=ahci.0,drive=hd"
    echo "pc-$cpu-1c|-machine pc$ACCELARG -cpu $cpu -smp 1 -m 1024 -device rtl8139,netdev=n0 -netdev user,id=n0 -drive file=$BLANK,format=raw,if=ide,index=0"
  done

  # SMP + HT topologies (the lapic-id-as-index class of bug).
  echo "smp-1|-machine q35$ACCELARG -smp 1 -m 2048"
  echo "smp-2|-machine q35$ACCELARG -smp 2 -m 2048"
  echo "smp-4|-machine q35$ACCELARG -smp 4 -m 2048"
  echo "smp-8|-machine q35$ACCELARG -smp 8 -m 4096"
  echo "smp-16|-machine q35$ACCELARG -smp 16 -m 4096"
  echo "ht-2x1x2|-machine q35$ACCELARG -smp 4,sockets=2,cores=1,threads=2 -m 4096"
  echo "ht-1x2x2|-machine q35$ACCELARG -smp 4,sockets=1,cores=2,threads=2 -m 4096"
  echo "ht-2x2x2|-machine q35$ACCELARG -smp 8,sockets=2,cores=2,threads=2 -m 8192"
  echo "ht-1x4x2|-machine q35$ACCELARG -smp 8,sockets=1,cores=4,threads=2 -m 4096"

  # Memory sizes.
  for m in 512 1024 3072 4096 8192; do
    echo "mem-${m}m|-machine q35$ACCELARG -smp 2 -m $m"
  done

  # NICs.
  for nic in e1000 e1000e rtl8139 virtio-net-pci ne2k_pci pcnet; do
    echo "nic-$nic|-machine q35$ACCELARG -smp 2 -m 2048 -device $nic,netdev=n0 -netdev user,id=n0"
  done
  echo "nic-none|-machine q35$ACCELARG -smp 2 -m 2048 -net none"

  # Disk controllers.
  echo "disk-ide|-machine pc$ACCELARG -smp 2 -m 2048 -drive file=$BLANK,format=raw,if=ide,index=0"
  echo "disk-ahci|-machine q35$ACCELARG -smp 2 -m 2048 -device ich9-ahci,id=ahci -drive id=hd,file=$BLANK,format=raw,if=none -device ide-hd,bus=ahci.0,drive=hd"
  echo "disk-nvme|-machine q35$ACCELARG -smp 2 -m 2048 -drive id=nv,file=$BLANK,format=raw,if=none -device nvme,serial=CRV,drive=nv"
  echo "disk-virtio|-machine q35$ACCELARG -smp 2 -m 2048 -drive id=vd,file=$BLANK,format=raw,if=none -device virtio-blk-pci,drive=vd"
  echo "disk-usb|-machine q35$ACCELARG -smp 2 -m 2048 -device qemu-xhci,id=xhci -drive id=ud,file=$BLANK,format=raw,if=none -device usb-storage,bus=xhci.0,drive=ud"

  # USB host controllers.
  echo "usb-uhci|-machine pc$ACCELARG -smp 2 -m 2048 -device piix3-usb-uhci -device usb-kbd -device usb-mouse"
  echo "usb-ehci|-machine q35$ACCELARG -smp 2 -m 2048 -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0 -device usb-mouse,bus=ehci.0"
  echo "usb-xhci-qemu|-machine q35$ACCELARG -smp 2 -m 2048 -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0"
  echo "usb-xhci-nec|-machine q35$ACCELARG -smp 2 -m 2048 -device nec-usb-xhci,id=xhci -device usb-kbd,bus=xhci.0"
  echo "usb-mixed|-machine pc$ACCELARG -smp 2 -m 2048 -usb -device piix3-usb-uhci -device usb-ehci,id=e -device qemu-xhci,id=x -device usb-tablet"

  # Display adapters (framebuffer path).
  for vga in std cirrus vmware qxl virtio; do
    echo "vga-$vga|-machine q35$ACCELARG -smp 2 -m 2048 -vga $vga"
  done

  # Timer / firmware edge cases.
  echo "no-hpet|-machine q35$ACCELARG,hpet=off -smp 2 -m 2048"
  echo "pc-hpet-off|-machine pc$ACCELARG,hpet=off -smp 2 -m 2048"
  echo "rtc-utc|-machine q35$ACCELARG -smp 2 -m 2048 -rtc base=utc,clock=vm"
  echo "tsc-noinvariant|-machine q35 -accel tcg -cpu qemu64,-invtsc -smp 2 -m 2048"

  # Whole-desktop and whole-laptop shapes (everything together).
  echo "desktop-full|-machine q35$ACCELARG -cpu Skylake-Client -smp 6,sockets=1,cores=6,threads=1 -m 8192 -device e1000e,netdev=n0 -netdev user,id=n0 -device qemu-xhci,id=x -device usb-kbd,bus=x.0 -device usb-mouse,bus=x.0 -device nvme,serial=CRV,drive=nv -drive id=nv,file=$BLANK,format=raw,if=none -vga qxl"
  echo "laptop-full|-machine q35$ACCELARG -cpu Haswell -smp 4,sockets=1,cores=2,threads=2 -m 4096 -device e1000,netdev=n0 -netdev user,id=n0 -device usb-ehci,id=e -device usb-kbd,bus=e.0 -device ich9-ahci,id=ahci -drive id=hd,file=$BLANK,format=raw,if=none -device ide-hd,bus=ahci.0,drive=hd -vga vmware"
  echo "ancient-pc|-machine pc$ACCELARG -cpu Conroe -smp 1 -m 512 -device rtl8139,netdev=n0 -netdev user,id=n0 -device piix3-usb-uhci -vga cirrus -drive file=$BLANK,format=raw,if=ide,index=0"
}

# ---- run one profile ------------------------------------------------------
# Serial-to-file is buffered while QEMU runs, so we give each machine a fixed
# boot budget, let it run, then classify the flushed log. Under KVM a healthy
# boot reaches userspace in ~1-2s, so the budget is many times the norm and a
# log with no boot marker means a real stall, not a slow machine.
run_one() {
  name=$1; args=$2
  log="$OUT/$name.serial.log"
  : > "$log"
  # shellcheck disable=SC2086
  timeout -k 2 "$TIMEOUT" qemu-system-x86_64 \
      -cdrom "$ISO" -boot d \
      -serial "file:$log" -display none -no-reboot -no-shutdown \
      $args >/dev/null 2>&1 || true

  status=TIMEOUT
  if grep -qiE "kernel panic|-> unrecoverable|Machine Check|Unknown Exception|Triple fault|GPF|General Protection Fault \\(kernel\\)" "$log" 2>/dev/null; then
    status=PANIC
  elif grep -qE "shell spawned|launching installer|no disk -> live mode" "$log" 2>/dev/null; then
    status=PASS
  elif [ ! -s "$log" ]; then
    status=NOBOOT
  fi

  boott=$(grep -m1 -E "shell spawned|launching installer|live mode" "$log" 2>/dev/null | sed -n 's/^\[ *\([0-9.]*\)\].*/\1/p')
  [ -n "$boott" ] || boott="-"

  ufaults=$(grep -F "Page Fault in userspace" "$log" 2>/dev/null | wc -l)
  smp=$(grep -F "entering idle loop" "$log" 2>/dev/null | wc -l)

  note=""
  [ "$ufaults" -gt 0 ] && note="userspace-faults=$ufaults "
  [ "$smp" -gt 0 ] && note="${note}cpus=$smp"
  printf '%-22s %-8s %8ss  %s\n' "$name" "$status" "$boott" "$note" | tee -a "$REPORT"
}

echo "Cervus hardware sweep — accel=$ACCEL timeout=${TIMEOUT}s" | tee "$REPORT"
echo "ISO: $ISO" | tee -a "$REPORT"
printf '%-22s %-8s %10s  %s\n' "PROFILE" "STATUS" "BOOT" "NOTES" | tee -a "$REPORT"
echo "------------------------------------------------------------------" | tee -a "$REPORT"

total=0; pass=0; fail=0
while IFS='|' read -r name args; do
  [ -n "$name" ] || continue
  case "$name" in $PATTERN) ;; *) continue ;; esac
  total=$((total + 1))
  run_one "$name" "$args"
  st=$(tail -1 "$REPORT" | awk '{print $2}')
  if [ "$st" = PASS ]; then pass=$((pass + 1)); else fail=$((fail + 1)); fi
done <<EOF
$(profiles)
EOF

echo "------------------------------------------------------------------" | tee -a "$REPORT"
echo "total=$total pass=$pass fail=$fail" | tee -a "$REPORT"
echo "logs and report in $OUT" | tee -a "$REPORT"
[ "$fail" -eq 0 ]
