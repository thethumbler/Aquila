# libICE, the Inter-Client Exchange protocol (for session management), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libice
VERSION=1.1.2
URL=https://www.x.org/archive/individual/lib/libICE-$VERSION.tar.xz
SHA256=974e4ed414225eb3c716985df9709f4da8d22a67a2890066bc6dfc89ad298625
ARCHIVE_DIR=libICE-$VERSION
DEPENDS="xorgproto xtrans"

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --disable-docs --disable-specs --without-xmlto --without-fop --disable-ipv6
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
