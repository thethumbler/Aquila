#!/bin/sh
#
# A tiny disk that boots AquilaOS (make live): Limine on a FAT partition,
# the kernel (stripped) and the initrd gzip'd, which Limine decompresses;
# the root filesystem is the initrd, in memory. Made for v86, which runs
# it in a browser (aquilaos.com's live system).
#
# usage: live.sh <image> <size in KiB> <kernel> <initrd> <limine.conf>
#        <limine share dir> <limine program> <strip>
#
# The partition starts at sector 63, the first Limine's second stage
# leaves free. Needs mtools (mformat, mmd, mcopy) and sfdisk.
#

set -eu

img=$1 size=$2 kernel=$3 initrd=$4 conf=$5 share=$6 limine=$7 strip=$8

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$strip" -o "$tmp/kernel" "$kernel"
gzip -9 -n < "$tmp/kernel" > "$tmp/kernel.gz"
gzip -9 -n < "$initrd" > "$tmp/initrd.gz"

sectors=$((size * 2))

rm -f "$img"
dd if=/dev/zero of="$img" bs=1024 count="$size" status=none
echo 'start=63, type=0c, bootable' | sfdisk -q --label dos "$img"

# the partition, through mtools (its offset in bytes)
part="$img@@$((63 * 512))"

export MTOOLS_SKIP_CHECK=1
mformat -i "$part" -T $((sectors - 63)) -h 16 -s 63 -v AQUILA ::
mmd -i "$part" ::/boot ::/boot/limine
mcopy -i "$part" "$conf" ::/boot/limine/limine.conf
mcopy -i "$part" "$share/limine-bios.sys" ::/boot/limine/
mcopy -i "$part" "$tmp/kernel.gz" "$tmp/initrd.gz" ::/boot/

# (what it says only if it fails)
if ! out=$("$limine" bios-install "$img" 2>&1); then
    echo "$out" >&2
    exit 1
fi

free=$(mdir -i "$part" :: | sed -n 's/^ *\([0-9 ]*\) bytes free/\1/p' | tr -d ' ')
echo "  live: kernel $(wc -c < "$tmp/kernel.gz") bytes, initrd $(wc -c < "$tmp/initrd.gz") bytes (gzip'd), $free bytes free"
