#!/bin/bash
#
# Build the i686-aquila cross toolchain and C library.
#
# usage: toolchain/build.sh [stage...]
#
# stages (default: all, in this order):
#   fetch       download and verify the source archives
#   autotools   autoconf/automake versions newlib needs to regenerate its
#               build system (installed into the tools prefix)
#   binutils    i686-aquila binutils
#   gcc         i686-aquila gcc (C compiler and libgcc)
#   headers     install the user/kernel ABI headers (include/uapi)
#   libc        newlib with the aquila port (libc/) into the sysroot
#   limine      Limine bootloader (BIOS) and its host tool, for the ISO image
#
# environment:
#   BUILDDIR    output directory (default: <repo>/build)
#   JOBS        parallel jobs (default: nproc)
#
# Results:
#   $BUILDDIR/toolchain     cross tools, add $BUILDDIR/toolchain/bin to PATH
#   $BUILDDIR/sysroot       target headers and libraries (/usr/include, /usr/lib)
#
# Host requirements: a C/C++ compiler, make, bison, flex, texinfo, GMP, MPFR,
# MPC, wget, and nasm for Limine.
#

set -euo pipefail

# built with the host defaults, not the kernel settings exported by make
unset CC CXX CPP AS LD AR RANLIB CFLAGS CXXFLAGS CPPFLAGS ASFLAGS LDFLAGS LDLIBS

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDDIR=${BUILDDIR:-$ROOT/build}
JOBS=${JOBS:-$(nproc)}

TARGET=i686-aquila
PREFIX=$BUILDDIR/toolchain
SYSROOT=$BUILDDIR/sysroot
DOWNLOADS=$BUILDDIR/downloads
SRC=$BUILDDIR/toolchain-src
OBJ=$BUILDDIR/toolchain-obj
PATCHES=$ROOT/toolchain/patches

BINUTILS=binutils-2.47
GCC=gcc-15.3.0
NEWLIB=newlib-4.6.0.20260123
LIMINE=limine-12.9.4
# newlib only regenerates with these exact versions
AUTOCONF=autoconf-2.69
AUTOMAKE=automake-1.15.1

declare -A URL=(
    [$BINUTILS]=https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz
    [$GCC]=https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz
    [$NEWLIB]=https://sourceware.org/pub/newlib/$NEWLIB.tar.gz
    [$LIMINE]=https://github.com/limine-bootloader/limine/releases/download/v${LIMINE#limine-}/$LIMINE.tar.xz
    [$AUTOCONF]=https://ftp.gnu.org/gnu/autoconf/$AUTOCONF.tar.xz
    [$AUTOMAKE]=https://ftp.gnu.org/gnu/automake/$AUTOMAKE.tar.xz
)

declare -A SHA256=(
    [$BINUTILS]=154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff
    [$GCC]=fa59c1beef8995f27c4d71c1df227587189315d3e6faff1bb4306e61b0c530eb
    [$NEWLIB]=6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3
    [$LIMINE]=0b576463ec818d09df5dfd27d06cf3028be54bd95b12007053cc0818c041f4c3
    [$AUTOCONF]=64ebcec9f8ac5b2487125a86a7760d2591ac9e1d3dbd59489633f9de62a57684
    [$AUTOMAKE]=af6ba39142220687c500f79b4aa2f181d9b24e4f8d8ec497cea4ba26c64bedaf
)

export PATH=$PREFIX/bin:$PATH

log()
{
    echo "==> $*"
}

archive()
{
    echo "$DOWNLOADS/$(basename "${URL[$1]}")"
}

# extract a pristine copy of a package and apply its patch, if any
unpack()
{
    local pkg=$1

    rm -rf "${SRC:?}/$pkg"
    mkdir -p "$SRC"
    tar -xf "$(archive "$pkg")" -C "$SRC"

    if [ -f "$PATCHES/$pkg-aquila.patch" ]; then
        log "patching $pkg"
        patch -d "$SRC/$pkg" -p1 -s < "$PATCHES/$pkg-aquila.patch"
    fi
}

stage_fetch()
{
    mkdir -p "$DOWNLOADS"

    for pkg in "${!URL[@]}"; do
        local file
        file=$(archive "$pkg")

        if [ ! -f "$file" ]; then
            log "downloading $pkg"
            wget -q -O "$file.part" "${URL[$pkg]}"
            mv "$file.part" "$file"
        fi

        echo "${SHA256[$pkg]}  $file" | sha256sum -c --quiet - || {
            echo "checksum mismatch for $file" >&2
            exit 1
        }
    done
}

stage_autotools()
{
    for pkg in $AUTOCONF $AUTOMAKE; do
        log "building $pkg"
        unpack $pkg
        (cd "$SRC/$pkg" && ./configure --prefix="$PREFIX/autotools" >/dev/null &&
            make -j"$JOBS" >/dev/null && make install >/dev/null)
    done
}

stage_binutils()
{
    log "building $BINUTILS"
    unpack $BINUTILS

    rm -rf "${OBJ:?}/binutils" && mkdir -p "$OBJ/binutils"
    cd "$OBJ/binutils"

    "$SRC/$BINUTILS/configure" \
        --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" \
        --disable-nls --disable-werror --disable-gdb --disable-gprofng
    make -j"$JOBS"
    make install
}

stage_gcc()
{
    log "building $GCC"
    unpack $GCC

    rm -rf "${OBJ:?}/gcc" && mkdir -p "$OBJ/gcc"
    cd "$OBJ/gcc"

    # --with-newlib: build libgcc without needing the C library first
    "$SRC/$GCC/configure" \
        --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" \
        --with-native-system-header-dir=/usr/include --with-newlib \
        --enable-languages=c --disable-nls --disable-multilib \
        --disable-libssp --disable-libquadmath --disable-libgomp \
        --disable-libatomic --disable-libstdcxx
    make -j"$JOBS" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc

    # gcc's <limits.h> only includes the C library's (PATH_MAX, SSIZE_MAX,
    # ...) if the sysroot had one when gcc was built, newlib comes later:
    # install the version gcc would have made
    local inc
    inc=$($TARGET-gcc -print-file-name=include)
    cat "$SRC/$GCC/gcc/limitx.h" "$SRC/$GCC/gcc/glimits.h" "$SRC/$GCC/gcc/limity.h" \
        > "$inc/limits.h"
    cp "$SRC/$GCC/gcc/gsyslimits.h" "$inc/syslimits.h"
}

stage_headers()
{
    log "installing ABI headers into the sysroot"
    mkdir -p "$SYSROOT/usr/include"
    cp -r "$ROOT/include/uapi/." "$SYSROOT/usr/include/"
}

stage_libc()
{
    log "building $NEWLIB"
    unpack $NEWLIB

    # the aquila port lives in libc/, regenerate newlib's build system
    cp -r "$ROOT/libc/aquila" "$SRC/$NEWLIB/newlib/libc/sys/aquila"
    (cd "$SRC/$NEWLIB/newlib" &&
        PATH=$PREFIX/autotools/bin:$PATH autoreconf -f)

    rm -rf "${OBJ:?}/newlib" && mkdir -p "$OBJ/newlib"
    cd "$OBJ/newlib"

    "$SRC/$NEWLIB/configure" --target=$TARGET --prefix=/usr \
        --disable-multilib --enable-newlib-io-long-long \
        --enable-newlib-io-c99-formats
    make -j"$JOBS" all
    # install into /usr of the sysroot, not /usr/$TARGET
    make DESTDIR="$SYSROOT" tooldir=/usr install

    # crti/crtn delimit the .init/.fini sections around libgcc's crtbegin/crtend
    for f in crti crtn; do
        $TARGET-gcc -c "$ROOT/libc/aquila/$f.S" -o "$SYSROOT/usr/lib/$f.o"
    done

    log "checking the C library against the kernel ABI"
    $TARGET-gcc -Werror -fsyntax-only "$ROOT/libc/aquila/abi-check.c"
}

stage_limine()
{
    log "building $LIMINE"
    unpack $LIMINE

    # the BIOS stages are built with the i686-aquila toolchain
    cd "$SRC/$LIMINE"
    ./configure --prefix="$PREFIX" --enable-bios --enable-bios-cd \
        TOOLCHAIN_FOR_TARGET=$TARGET-
    make -j"$JOBS"
    make install
}

ALL_STAGES="fetch autotools binutils gcc headers libc limine"
stages=${*:-$ALL_STAGES}

for stage in $stages; do
    case " $ALL_STAGES " in
        *" $stage "*) stage_$stage ;;
        *) echo "unknown stage: $stage" >&2; exit 2 ;;
    esac
done

log "done: $PREFIX/bin/$TARGET-gcc"
