# libXaw, the Athena widgets (xterm's menus, scrollbars), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxaw
VERSION=1.0.16
URL=https://www.x.org/archive/individual/lib/libXaw-$VERSION.tar.xz
SHA256=731d572b54c708f81e197a6afa8016918e2e06dfd3025e066ca642a5b8c39c8f
ARCHIVE_DIR=libXaw-$VERSION
DEPENDS="libxmu libxpm"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-specs --without-xmlto --without-fop --disable-xaw6
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
