# libXpm, X pixmaps (images), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxpm
VERSION=3.5.17
URL=https://www.x.org/archive/individual/lib/libXpm-$VERSION.tar.xz
SHA256=64b31f81019e7d388c822b0b28af8d51c4622b83f1f0cb6fa3fc95e271226e43
ARCHIVE_DIR=libXpm-$VERSION
DEPENDS="libx11 libxext libxt"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-open-zfile --disable-stat-zfile
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
