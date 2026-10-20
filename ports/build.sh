#!/bin/bash
#
# Build third-party software for AquilaOS.
#
# usage: ports/build.sh [port...]   (default: all ports)
#
# Each port is ports/<name>/recipe.sh, which sets NAME, VERSION, URL and
# SHA256 and defines a build() function. build() runs in the extracted
# source directory (after applying ports/<name>/*.patch) with CC, AR,
# RANLIB, STRIP set for the i686-aquila toolchain, and installs into
# $DESTDIR, which the initrd includes (make initrd).
#
# environment:
#   BUILDDIR    output directory (default: <repo>/build)
#   TOOLCHAIN   i686-aquila toolchain (default: $BUILDDIR/toolchain)
#   JOBS        parallel jobs (default: nproc)
#

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDDIR=${BUILDDIR:-$ROOT/build}
TOOLCHAIN=${TOOLCHAIN:-$BUILDDIR/toolchain}
JOBS=${JOBS:-$(nproc)}

TARGET=i686-aquila
DOWNLOADS=$BUILDDIR/downloads
SRC=$BUILDDIR/ports/src
export DESTDIR=$BUILDDIR/ports/root

export PATH=$TOOLCHAIN/bin:$PATH
export CC=$TARGET-gcc AR=$TARGET-ar RANLIB=$TARGET-ranlib STRIP=$TARGET-strip
export JOBS

if ! command -v $CC >/dev/null; then
    echo "error: $CC not found, run 'make toolchain' first" >&2
    exit 1
fi

build_port()
{
    local recipe=$ROOT/ports/$1/recipe.sh

    if [ ! -f "$recipe" ]; then
        echo "unknown port: $1" >&2
        exit 2
    fi

    (
        # shellcheck source=/dev/null
        source "$recipe"

        local archive=$DOWNLOADS/$(basename "$URL")

        mkdir -p "$DOWNLOADS" "$SRC" "$DESTDIR"

        if [ ! -f "$archive" ]; then
            echo "==> downloading $NAME-$VERSION"
            wget -q -O "$archive.part" "$URL"
            mv "$archive.part" "$archive"
        fi

        echo "$SHA256  $archive" | sha256sum -c --quiet -

        echo "==> building $NAME-$VERSION"
        rm -rf "${SRC:?}/$NAME-$VERSION"
        tar -xf "$archive" -C "$SRC"
        cd "$SRC/$NAME-$VERSION"

        for p in "$ROOT/ports/$1"/*.patch; do
            [ -f "$p" ] && patch -p1 -s < "$p"
        done

        build
    )
}

for port in ${*:-$(cd "$ROOT/ports" && ls -d */ | tr -d /)}; do
    build_port "$port"
done
