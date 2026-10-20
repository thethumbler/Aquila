#!/bin/bash
#
# Build third-party software for AquilaOS.
#
# usage: ports/build.sh [port...]   (default: all ports)
#
# Each port is ports/<name>/recipe.sh, which sets NAME, VERSION, URL and
# SHA256 and defines a build() function, and optionally:
#
#   DEPENDS     ports to build first
#   FILE        name of the downloaded archive (default: basename of URL)
#
# Sources that have no usable release archive come from git instead: GIT
# (repository) and COMMIT (full hash, which pins the contents) replace URL
# and SHA256.
#
# build() runs in the extracted source directory (after applying
# ports/<name>/*.patch) with CC, AR, RANLIB, STRIP set for the i686-aquila
# toolchain, and AQUILA_UAPI pointing at the kernel ABI headers (for host
# tools that generate target tables). Programs are installed into $DESTDIR, which the initrd
# includes (make initrd); libraries and headers other ports build against
# into $STAGING (CPPFLAGS and LDFLAGS point there).
#
# environment:
#   BUILDDIR    output directory (default: <repo>/build)
#   TOOLCHAIN   i686-aquila toolchain (default: $BUILDDIR/toolchain)
#   JOBS        parallel jobs (default: nproc)
#

set -euo pipefail

# don't inherit the kernel settings exported by make
unset CFLAGS CXXFLAGS CPPFLAGS ASFLAGS LDFLAGS LDLIBS LD AS

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDDIR=${BUILDDIR:-$ROOT/build}
TOOLCHAIN=${TOOLCHAIN:-$BUILDDIR/toolchain}
JOBS=${JOBS:-$(nproc)}

TARGET=i686-aquila
DOWNLOADS=$BUILDDIR/downloads
SRC=$BUILDDIR/ports/src
export DESTDIR=$BUILDDIR/ports/root
export STAGING=$BUILDDIR/ports/staging

export PATH=$TOOLCHAIN/bin:$PATH
export CC=$TARGET-gcc AR=$TARGET-ar RANLIB=$TARGET-ranlib STRIP=$TARGET-strip
export JOBS
export AQUILA_UAPI=$ROOT/include/uapi
export CPPFLAGS="-I$STAGING/usr/include" LDFLAGS="-L$STAGING/usr/lib"

if ! command -v $CC >/dev/null; then
    echo "error: $CC not found, run 'make toolchain' first" >&2
    exit 1
fi

declare -A built

build_port()
{
    local recipe=$ROOT/ports/$1/recipe.sh

    [ -n "${built[$1]:-}" ] && return

    if [ ! -f "$recipe" ]; then
        echo "unknown port: $1" >&2
        exit 2
    fi

    # dependencies first
    local dep
    for dep in $(DEPENDS=; source "$recipe"; echo "$DEPENDS"); do
        build_port "$dep"
    done

    (
        # shellcheck source=/dev/null
        source "$recipe"

        mkdir -p "$DOWNLOADS" "$SRC" "$DESTDIR" "$STAGING"

        if [ -n "${GIT:-}" ]; then
            local archive=$DOWNLOADS/$NAME-$COMMIT.tar
            local repo=$DOWNLOADS/$NAME.git

            # a branch or tag name could change what is built
            if ! [[ $COMMIT =~ ^[0-9a-f]{40}$ ]]; then
                echo "$NAME: COMMIT must be a full commit hash" >&2
                exit 1
            fi

            if [ ! -f "$archive" ]; then
                echo "==> downloading $NAME-$VERSION"
                [ -d "$repo" ] || git clone -q --bare "$GIT" "$repo"
                git -C "$repo" cat-file -e "$COMMIT^{commit}" 2>/dev/null ||
                    git -C "$repo" fetch -q origin
                git -C "$repo" archive --prefix="$NAME-$VERSION/" -o "$archive.part" "$COMMIT"
                mv "$archive.part" "$archive"
            fi
        else
            local archive=$DOWNLOADS/${FILE:-$(basename "$URL")}

            if [ ! -f "$archive" ]; then
                echo "==> downloading $NAME-$VERSION"
                wget -q -O "$archive.part" "$URL"
                mv "$archive.part" "$archive"
            fi

            echo "$SHA256  $archive" | sha256sum -c --quiet -
        fi

        echo "==> building $NAME-$VERSION"
        rm -rf "${SRC:?}/$NAME-$VERSION"
        tar -xf "$archive" -C "$SRC"
        cd "$SRC/$NAME-$VERSION"

        for p in "$ROOT/ports/$1"/*.patch; do
            [ -f "$p" ] && patch -p1 -s < "$p"
        done

        build
    )

    built[$1]=1
}

for port in ${*:-$(cd "$ROOT/ports" && ls -d */ | tr -d /)}; do
    build_port "$port"
done
