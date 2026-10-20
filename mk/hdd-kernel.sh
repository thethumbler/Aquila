#!/bin/bash
#
# Put a kernel and Limine configuration on a hard disk image made by
# mk/hdd.sh, in its /boot (the first partition). The root filesystem, and
# what was written to it, stays as it is.
#
# usage: mk/hdd-kernel.sh <image> <kernel> <limine.conf>
#
# Needs sfdisk and mtools.
#

set -euo pipefail

IMG=$1 KERNEL=$2 CONF=$3

for tool in sfdisk mcopy; do
    if ! command -v $tool >/dev/null; then
        echo "error: $tool not found" >&2
        exit 1
    fi
done

# the first partition's start, in sectors
start=$(sfdisk -d "$IMG" | sed -n 's/^.*1 : start= *\([0-9]*\),.*/\1/p')

if [ -z "$start" ]; then
    echo "error: $IMG has no first partition" >&2
    exit 1
fi

boot="$IMG@@$((start * 512))"

mcopy -o -i "$boot" "$KERNEL" ::/boot/kernel
mcopy -o -i "$boot" "$CONF" ::/boot/limine/limine.conf
