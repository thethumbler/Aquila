#!/bin/bash
#
# Put a root filesystem tree (build/rootfs) on a hard disk image made by
# mk/hdd.sh, in its root filesystem (the second partition): the files,
# symbolic links and directories of the tree replace those on the disk,
# owned by root. The rest of the disk is kept (/root, /home, what was
# made on it), and so are the accounts if the disk has them already:
# /etc/passwd, /etc/group, /etc/shadow and /etc/hostname.
#
# usage: mk/hdd-update.sh <image> <rootfs dir>
#
# The disk must not be in use (QEMU). Needs sfdisk, debugfs, e2fsck and
# python3.
#

set -euo pipefail

IMG=$1 ROOTFS=$2

KEEP="etc/passwd etc/group etc/shadow etc/hostname"

for tool in sfdisk debugfs e2fsck python3; do
    if ! command -v $tool >/dev/null; then
        echo "error: $tool not found" >&2
        exit 1
    fi
done

# QEMU locks the images it has open
if ! python3 -I -c 'import fcntl, sys
f = open(sys.argv[1], "rb+")
fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB)' "$IMG" 2>/dev/null; then
    echo "error: $IMG is in use" >&2
    exit 1
fi

# the second partition's start and size, in sectors
read -r start size < <(sfdisk -d "$IMG" |
    sed -n 's/^.*2 : start= *\([0-9]*\), size= *\([0-9]*\),.*/\1 \2/p')

if [ -z "${start:-}" ]; then
    echo "error: $IMG has no second partition" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

root=$tmp/root.img
dd if="$IMG" of="$root" bs=512 skip="$start" count="$size" status=none

if find "$ROOTFS" -name '* *' | grep -q .; then
    echo "error: $ROOTFS has names with spaces, debugfs can't take them" >&2
    exit 1
fi

# what the disk has: its directories, and the files kept (debugfs mkdir of
# a directory that exists leaves an inode behind)
(cd "$ROOTFS" && find . -mindepth 1 -type d | sed 's|^\.||'; for k in $KEEP; do echo "/$k"; done) |
    sed 's/^/stat /' > "$tmp/stats"

debugfs -f "$tmp/stats" "$root" 2>&1 |
    awk '/^debugfs: stat / { path = $3 } /^Inode:/ { print path, $4 }' > "$tmp/exists"

exists()
{
    grep -q -x -F "$1 $2" "$tmp/exists"
}

# directories before what is in them
{
    cd "$ROOTFS"
    find . -mindepth 1 | sed 's|^\./||' | sort | while read -r path; do
        if [ -d "$path" ] && [ ! -L "$path" ]; then
            exists "/$path" directory || echo "mkdir /$path"
            printf 'sif /%s mode 0%o\n' "$path" "$((0x$(stat -c %f "$path")))"
        elif [ -L "$path" ]; then
            echo "rm /$path"
            echo "symlink /$path $(readlink "$path")"
        elif [ -f "$path" ]; then
            case " $KEEP " in
                *" $path "*) exists "/$path" regular && continue ;;
            esac
            echo "rm /$path"
            echo "write $PWD/$path /$path"
        fi
    done
} > "$tmp/cmds"

debugfs -w -f "$tmp/cmds" "$root" > "$tmp/log" 2>&1

# rm of what isn't there yet is expected
if grep -v -E '^debugfs|File not found by ext2_lookup|^Allocated inode' "$tmp/log" |
        grep -q .; then
    echo "debugfs:" >&2
    grep -v -E '^debugfs|File not found by ext2_lookup|^Allocated inode' "$tmp/log" |
        head -20 >&2
    exit 1
fi

# only a clean filesystem goes back on the disk
if ! e2fsck -fn "$root" > "$tmp/e2fsck.log" 2>&1; then
    echo "error: the updated root filesystem isn't clean, $IMG is left as it was:" >&2
    head -20 "$tmp/e2fsck.log" >&2
    exit 1
fi

dd if="$root" of="$IMG" bs=512 seek="$start" conv=notrunc status=none
