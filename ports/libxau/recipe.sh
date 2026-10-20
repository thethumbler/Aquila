# libXau, X authorization files (.Xauthority), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxau
VERSION=1.0.12
URL=https://www.x.org/archive/individual/lib/libXau-$VERSION.tar.xz
SHA256=74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb
ARCHIVE_DIR=libXau-$VERSION
DEPENDS="xorgproto"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
