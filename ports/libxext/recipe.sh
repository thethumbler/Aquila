# libXext, the client side of X extensions (shape, ...), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxext
VERSION=1.3.6
URL=https://www.x.org/archive/individual/lib/libXext-$VERSION.tar.xz
SHA256=edb59fa23994e405fdc5b400afdf5820ae6160b94f35e3dc3da4457a16e89753
ARCHIVE_DIR=libXext-$VERSION
DEPENDS="libx11"

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --disable-specs --without-xmlto --without-fop --disable-malloc0returnsnull
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
