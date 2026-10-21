# libXt, the X Toolkit Intrinsics, https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxt
VERSION=1.3.1
URL=https://www.x.org/archive/individual/lib/libXt-$VERSION.tar.xz
SHA256=e0a774b33324f4d4c05b199ea45050f87206586d81655f8bef4dba434d931288
ARCHIVE_DIR=libXt-$VERSION
DEPENDS="libx11 libsm"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-specs --without-xmlto --without-fop --without-glib
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
