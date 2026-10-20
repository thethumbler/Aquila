#!/bin/bash
#
# Build a hard disk image of AquilaOS, without root privileges:
#
#   MBR          partition table, Limine's BIOS boot code (and its stage 2
#                in the gap before the first partition)
#   partition 1  FAT32, /boot: the kernel and Limine (which reads only FAT)
#   partition 2  ext2, the root filesystem, /dev/hda2 to the kernel
#
# usage: mk/hdd.sh <image> <size MiB> <rootfs dir> <kernel> <limine.conf>
#                  <limine dir> <limine tool>
#
# <limine dir> has limine-bios.sys, <limine tool> is Limine's host tool
# (both from make toolchain). Needs sfdisk, mkfs.fat, mtools, mke2fs and
# fakeroot.
#

set -euo pipefail

IMG=$1 SIZE=$2 ROOTFS=$3 KERNEL=$4 CONF=$5 LIMINE=$6 TOOL=$7

BOOT_MB=64
START=2048                          # 1 MiB, room for Limine's stage 2
ROOT_START=$((START + BOOT_MB * 2048))
ROOT_SECTORS=$((SIZE * 2048 - ROOT_START))

if [ "$ROOT_SECTORS" -le 0 ]; then
    echo "error: $SIZE MiB is too small, the boot partition takes $BOOT_MB" >&2
    exit 1
fi

for tool in sfdisk mkfs.fat mcopy mke2fs fakeroot; do
    if ! command -v $tool >/dev/null; then
        echo "error: $tool not found" >&2
        exit 1
    fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

rm -f "$IMG"
truncate -s "${SIZE}M" "$IMG"

sfdisk -q "$IMG" <<END
label: dos
start=$START, size=$((BOOT_MB * 2048)), type=c, bootable
start=$ROOT_START, type=83
END

# /boot
truncate -s "${BOOT_MB}M" "$tmp/boot.img"
mkfs.fat -F 32 -n AQUILABOOT "$tmp/boot.img" >/dev/null
mmd -i "$tmp/boot.img" ::/boot ::/boot/limine
mcopy -i "$tmp/boot.img" "$KERNEL" ::/boot/kernel
mcopy -i "$tmp/boot.img" "$LIMINE/limine-bios.sys" ::/boot/limine/
mcopy -i "$tmp/boot.img" "$CONF" ::/boot/limine/limine.conf

# /, its files owned by root (1 KiB blocks, as the ext2 test uses)
fakeroot sh -c '
    cp -a "$1" "$2/root" && chown -R 0:0 "$2/root" &&
    mke2fs -q -t ext2 -b 1024 -L aquila -d "$2/root" "$2/root.img" "$3"k
' _ "$ROOTFS" "$tmp" $((ROOT_SECTORS / 2))

dd if="$tmp/boot.img" of="$IMG" bs=512 seek=$START conv=notrunc status=none
dd if="$tmp/root.img" of="$IMG" bs=512 seek=$ROOT_START conv=notrunc status=none

# its notes go to stderr, shown only if it fails
if ! "$TOOL" bios-install "$IMG" > "$tmp/limine.log" 2>&1; then
    cat "$tmp/limine.log" >&2
    exit 1
fi
