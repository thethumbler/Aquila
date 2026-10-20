#!/bin/bash
#
# Build the kernel with the host toolchain and run the QEMU test suite.
#
# usage: tests/run.sh [-n] [-v] [test...]
#
#   -n      don't rebuild the kernel, use the existing image
#   -v      print the serial log of failed tests
#
# tests: ata cpu syscalls ext2 libc system (default: all)
#
# Requires gcc with 32-bit support, ld, cpio and qemu-system-i386. The ext2
# test also needs e2fsprogs (mke2fs, e2fsck, debugfs) and python3. The libc
# and system tests need the i686-aquila toolchain (make toolchain) and are
# skipped without it.
#

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TESTS=$ROOT/tests
OUT=$TESTS/out
CONFIG=../tests/host/i386-pc

ALL_TESTS="ata cpu syscalls ext2 libc system"

CROSS_CC=$ROOT/build/toolchain/bin/i686-aquila-gcc

build=1
verbose=0

while getopts "nvh" opt; do
    case $opt in
        n) build=0 ;;
        v) verbose=1 ;;
        *) sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

selected=${*:-$ALL_TESTS}

for tool in gcc ld cpio qemu-system-i386; do
    if ! command -v $tool >/dev/null; then
        echo "error: $tool not found" >&2
        exit 2
    fi
done

mkdir -p "$OUT"

#
# Kernel
#

if [ $build = 1 ]; then
    echo "building kernel (CONFIG=$CONFIG)"
    if ! make -C "$ROOT" kernel CONFIG=$CONFIG BUILDDIR="$OUT/build" > "$OUT/build.log" 2>&1; then
        grep -E -B2 -A6 'error|Error' "$OUT/build.log" | head -40
        echo "error: kernel build failed, see $OUT/build.log" >&2
        exit 1
    fi
fi

KERNEL=$(ls "$OUT"/build/kernel/kernel-*.i386 2>/dev/null | head -1)

if [ -z "$KERNEL" ]; then
    echo "error: no kernel image, run without -n" >&2
    exit 1
fi

#
# Helpers
#

# build tests/<name>/init.c into an initrd with /init
build_initrd()
{
    local name=$1
    local root=$OUT/$name/root

    rm -rf "$root"
    mkdir -p "$root/dev" "$root/mnt" "$root/tmp"

    if [ "$name" = libc ]; then
        # a regular program, built against the C library
        "$CROSS_CC" -O1 -Wall -o "$root/init" "$TESTS/$name/init.c" \
            > "$OUT/$name/gcc.log" 2>&1 || { cat "$OUT/$name/gcc.log"; return 1; }
    else
        gcc -m32 -static -nostdlib -ffreestanding -fno-builtin -fno-pic -no-pie \
            -fno-stack-protector -fcf-protection=none -O1 -Wall -Wno-unused-function \
            -I"$ROOT/include/uapi" -Wl,--build-id=none -o "$root/init" "$TESTS/$name/init.c" \
            > "$OUT/$name/gcc.log" 2>&1 || { cat "$OUT/$name/gcc.log"; return 1; }
    fi

    # extra files for the test (e.g. etc/passwd)
    if [ -d "$TESTS/$name/rootfs" ]; then
        cp -r "$TESTS/$name/rootfs/." "$root/"
        [ -f "$root/etc/shadow" ] && chmod 600 "$root/etc/shadow"
    fi

    # the kernel's initramfs reads old binary cpio archives
    (cd "$root" && find . | cpio -o -H bin -R 0:0 --quiet) > "$OUT/$name/initrd.img"
}

# run_qemu <log> <seconds> <stop pattern> [qemu args...]
#
# Boots the kernel, stops once <stop pattern> or a kernel panic shows up
# in the serial log, or after <seconds>. Sets qemu_exited=1 if QEMU went
# away on its own, which with -no-reboot means the guest reset (e.g. a
# triple fault).
run_qemu()
{
    local log=$1 secs=$2 pattern=$3
    shift 3

    rm -f "$log"
    qemu-system-i386 -kernel "$KERNEL" -m 256 -display none -no-reboot \
        -serial file:"$log" "$@" >/dev/null 2>&1 &

    local pid=$! ticks=0
    qemu_exited=0

    while kill -0 $pid 2>/dev/null; do
        if grep -a -q -E "$pattern|Bailing out" "$log" 2>/dev/null; then
            break
        fi

        if [ $ticks -ge $((secs * 10)) ]; then
            break
        fi

        sleep 0.1
        ticks=$((ticks + 1))
    done

    if kill -0 $pid 2>/dev/null; then
        kill $pid
    else
        qemu_exited=1
    fi

    wait $pid 2>/dev/null
}

# common failure checks on a serial log, prints the reason
log_problems()
{
    local log=$1

    if grep -a -q 'Bailing out' "$log"; then
        echo "kernel panic:"
        grep -a -B3 'Bailing out' "$log" | sed 's/^/    /'
    elif [ $qemu_exited = 1 ]; then
        echo "QEMU exited, guest reset (triple fault?)"
    else
        return 1
    fi
}

# check aqtest output: all checks passed and the test finished
aqtest_result()
{
    local log=$1

    log_problems "$log" && return 1

    local pass fail
    pass=$(grep -a -c '^PASS' "$log")
    fail=$(grep -a -c '^FAIL' "$log")

    grep -a '^FAIL' "$log" | sed 's/^/    /'

    if ! grep -a -q 'aqtest: done' "$log"; then
        echo "did not finish ($pass passed, $fail failed), last output:"
        tail -3 "$log" | sed 's/^/    /'
        return 1
    fi

    echo "$pass passed, $fail failed"
    [ "$fail" = 0 ]
}

#
# Tests
#

# a disk on the first ATA channel is detected reliably
test_ata()
{
    local log=$OUT/ata/serial.log
    local disk=$OUT/ata/disk.img
    local runs=5 ok=0

    mkdir -p "$OUT/ata"
    rm -f "$disk"
    truncate -s 16M "$disk"

    for i in $(seq $runs); do
        run_qemu "$log" 10 'registered blkdev 3: ata' -initrd "$OUT/cpu/initrd.img" \
            -drive file="$disk",format=raw,index=0,media=disk

        log_problems "$log" && return 1

        grep -a -q 'ata0: model: QEMU HARDDISK' "$log" && ok=$((ok + 1))
    done

    echo "disk detected in $ok/$runs boots"
    [ $ok = $runs ]
}

test_cpu()
{
    run_qemu "$OUT/cpu/serial.log" 30 'aqtest: done' -initrd "$OUT/cpu/initrd.img"
    aqtest_result "$OUT/cpu/serial.log"
}

test_syscalls()
{
    run_qemu "$OUT/syscalls/serial.log" 60 'aqtest: done' -initrd "$OUT/syscalls/initrd.img"
    aqtest_result "$OUT/syscalls/serial.log"
}

test_ext2()
{
    local img=$OUT/ext2/disk.img
    local log=$OUT/ext2/serial.log

    for tool in mke2fs e2fsck debugfs python3; do
        if ! command -v $tool >/dev/null; then
            echo "SKIP: $tool not found"
            return 0
        fi
    done

    rm -f "$img"
    mke2fs -q -t ext2 -b 1024 -F "$img" 4M || return 1

    run_qemu "$log" 60 'aqtest: done' -initrd "$OUT/ext2/initrd.img" \
        -drive file="$img",format=raw,index=0,media=disk

    aqtest_result "$log" || return 1

    # check the image on the host
    if ! e2fsck -fn "$img" > "$OUT/ext2/e2fsck.log" 2>&1; then
        echo "e2fsck found errors:"
        sed 's/^/    /' "$OUT/ext2/e2fsck.log"
        return 1
    fi

    debugfs -R "dump /big $OUT/ext2/big.out" "$img" >/dev/null 2>&1

    python3 -I - "$OUT/ext2/big.out" <<'EOF' || return 1
import sys
data = open(sys.argv[1], 'rb').read()
want = bytes((i * 7 + (i >> 9)) & 0xff for i in range(305000))
if data != want:
    bad = next((i for i in range(min(len(data), len(want))) if data[i] != want[i]), None)
    print("file contents differ on the host: size %d, first mismatch at %s" % (len(data), bad))
    sys.exit(1)
EOF

    # what the test left: removed, renamed and unlinked-while-open files gone
    local names
    names=$(debugfs -R "ls /" "$img" 2>/dev/null | tr -s ' ' '\n' | grep -v '^[0-9(]' | sort | xargs)
    if [ "$names" != ". .. a b big lost+found sym trunc" ]; then
        echo "unexpected files on the image: $names"
        return 1
    fi

    echo "e2fsck clean, file contents and directory verified on the host"
}

# C library: errno, signals, stat and library functions through newlib
test_libc()
{
    if [ ! -x "$CROSS_CC" ]; then
        echo "SKIP: no i686-aquila toolchain, run make toolchain"
        return 0
    fi

    run_qemu "$OUT/libc/serial.log" 30 'aqtest: done' -initrd "$OUT/libc/initrd.img"
    aqtest_result "$OUT/libc/serial.log"
}

# the system built from source: logins on the serial console, run commands
#
# tests/system/rootfs adds a root password and an unprivileged guest user,
# dash and sbase (contrib/) come from the main build: make
test_system()
{
    local log=$OUT/system/serial.log
    local initrd=$OUT/system/initrd.img

    if [ ! -x "$CROSS_CC" ]; then
        echo "SKIP: no i686-aquila toolchain, run make toolchain"
        return 0
    fi

    # the base system's third-party software from the main build (make),
    # without the optional ports
    local contrib=$ROOT/build/contrib/root
    if [ ! -x "$contrib/bin/dash" ]; then
        echo "SKIP: contrib not built, run make"
        return 0
    fi

    mkdir -p "$OUT/system"
    make -C "$ROOT" initrd BUILDDIR="$OUT/system/build" TOOLCHAIN="$ROOT/build/toolchain" \
        CONTRIB_ROOT="$contrib" PORTS_ROOT=/nonexistent ROOTFS_OVERLAY="$TESTS/system/rootfs" \
        > "$OUT/system/build.log" 2>&1 || {
        tail -20 "$OUT/system/build.log"
        return 1
    }
    cp "$OUT/system/build/initrd.img" "$initrd"

    # output is checked for what the shell expanded, not the echoed input
    local long=0123456789012345678901234567890123456789012345678901234567890123456789

    # "seconds input": wait, then type the line on the serial console
    local session=(
        "6 root" "1 wrong-password"
        "6 guest" "1 guest-password" "5 echo user=\$USER" "1 cat /etc/shadow"
        "1 touch /tmp/g; ls -l /tmp/g" "1 echo via-\$((1 + 1))-tty > /dev/tty" "1 exit"
        "4 root" "1 root-password" "5 echo user=\$USER" "1 uname -a" "1 ls /bin" "1 ps"
        "1 cat /proc/meminfo" "1 touch /tmp/f" "1 ls /tmp" "1 cat /etc/shadow"
        "1 echo pipe-\$((6 * 7)) | cat | cat" "1 echo subst=\$(echo inner | cat)"
        "1 echo long:\$USER:$long:end"
        "1 cat" "1 "$'\x03' "1 echo status=\$?"
        "1 cd /tmp; mkdir d; echo sbase > d/f; cp -r d e; mv e/f e/g; ls e"
        "1 ln -s d/f l; cat l; rm -r d e; ls -l l"
        "1 seq 3 | sort -r | paste -sd, -" "1 yes | head -n 2 | wc -l"
    )

    {
        for step in "${session[@]}"; do
            sleep "${step%% *}"
            printf '%s\r' "${step#* }"
        done
        sleep 2
    } | timeout 90 qemu-system-i386 -kernel "$KERNEL" -initrd "$initrd --serial" \
        -append earlycon=ttyS0 -m 256 -display none -no-reboot -serial stdio \
        2>/dev/null | tr -d '\r' > "$log"

    if grep -a -q 'Bailing out' "$log"; then
        echo "kernel panic:"
        grep -a -B3 'Bailing out' "$log" | sed 's/^/    /'
        return 1
    fi

    local missing=0
    for want in 'Login incorrect' 'user=guest' 'shadow: Permission denied' \
                'user=root' 'AquilaOS aquila' 'aqbox' '/bin/ps' 'MemTotal' \
                '(^| )f( |$)' 'guest:\$6\$' 'pipe-42' 'subst=inner' \
                "long:root:$long:end" 'status=130' 'guest +guest .* /tmp/g' \
                '^g$' '^sbase$' 'l -> d/f' '^3,2,1$' '^2$' '^via-2-tty$'; do
        if ! grep -a -q -E -- "$want" "$log"; then
            echo "missing output: $want"
            missing=1
        fi
    done

    [ $missing = 0 ] && echo "wrong password rejected, guest denied /etc/shadow, root ran uname, ls, ps, cat, touch, pipelines, \$(...), a long line, ^C, /dev/tty and sbase"
}

#
# Main
#

failed=0

for name in $selected; do
    case " $ALL_TESTS " in
        *" $name "*) ;;
        *) echo "unknown test: $name" >&2; exit 2 ;;
    esac
done

# test programs (the ata test boots the cpu test's initrd)
needed=" $selected "
case "$needed" in *" ata "*) needed="$needed cpu " ;; esac
[ -x "$CROSS_CC" ] || needed=${needed/ libc / }

for name in cpu syscalls ext2 libc; do
    case "$needed" in
        *" $name "*)
            mkdir -p "$OUT/$name"
            build_initrd $name || { echo "error: failed to build $name test" >&2; exit 1; } ;;
    esac
done

for name in $selected; do
    printf '%-10s ' "$name"
    result=$(test_$name 2>&1)
    status=$?

    if [ $status = 0 ]; then
        echo "ok    $(echo "$result" | tail -1)"
    else
        echo "FAIL"
        echo "$result" | sed 's/^/    /'
        if [ $verbose = 1 ]; then
            echo "    --- serial log:"
            sed 's/^/    | /' "$OUT/$name/serial.log" 2>/dev/null | tail -40
        fi
        failed=$((failed + 1))
    fi
done

[ $failed = 0 ] && echo "all tests passed" || echo "$failed test(s) failed"
exit $failed
