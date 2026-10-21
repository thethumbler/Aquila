# libSM, X session management (for Xt), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libsm
VERSION=1.2.6
URL=https://www.x.org/archive/individual/lib/libSM-$VERSION.tar.xz
SHA256=be7c0abdb15cbfd29ac62573c1c82e877f9d4047ad15321e7ea97d1e43d835be
ARCHIVE_DIR=libSM-$VERSION
DEPENDS="libice"

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --disable-docs --without-xmlto --without-fop --without-libuuid --disable-ipv6
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
