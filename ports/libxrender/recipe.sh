# libXrender, the client side of RENDER (for Xft), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxrender
VERSION=0.9.12
URL=https://www.x.org/archive/individual/lib/libXrender-$VERSION.tar.xz
SHA256=b832128da48b39c8d608224481743403ad1691bf4e554e4be9c174df171d1b97
ARCHIVE_DIR=libXrender-$VERSION
DEPENDS="libx11"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-malloc0returnsnull
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
