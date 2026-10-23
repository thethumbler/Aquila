# UnixBench (byte-unixbench), the BYTE UNIX benchmarks,
# https://github.com/kdlucas/byte-unixbench
#
# The index's tests (Dhrystone, Whetstone, execl, file copies, pipes,
# context switches, process creation, shell scripts, system calls) in
# /usr/lib/unixbench, and ubench (its own ubench.c) in /usr/bin: what
# UnixBench's Run does for the index, without Perl. `ubench -c 4' runs
# 4 copies of each test at once. No graphics tests.

NAME=unixbench
VERSION=6.0.1
URL=https://github.com/kdlucas/byte-unixbench/archive/refs/tags/v$VERSION.tar.gz
FILE=byte-unixbench-$VERSION.tar.gz
ARCHIVE_DIR=byte-unixbench-$VERSION
SHA256=e52257ae9ba6294a0c5804d802745294cf536e8c8d9383aabff7a8b039e31faa

PROGS="dhry2reg whetstone-double execl fstime pipe context1 spawn looper syscall"

build()
{
    cd UnixBench

    # UnixBench's flags, but for this machine's CPU, not the host's
    # (-march=native)
    mkdir -p pgms
    make CC="$CC" OPTON="-O3 -ffast-math" $(printf 'pgms/%s ' $PROGS)

    local dir="$DESTDIR/usr/lib/unixbench"
    install -d "$dir/testdir"

    for p in $PROGS; do
        install -m 755 "pgms/$p" "$dir/"
        $STRIP "$dir/$p"
    done

    install -m 755 pgms/multi.sh pgms/tst.sh "$dir/"
    install -m 644 testdir/sort.src "$dir/testdir/"

    $CC -O2 -Wall -o ubench "$ROOT/ports/unixbench/ubench.c" -lm
    install -D -m 755 ubench "$DESTDIR/usr/bin/ubench"
    $STRIP "$DESTDIR/usr/bin/ubench"
}
