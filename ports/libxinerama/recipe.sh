# libXinerama, screens of a display (for dwm), https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxinerama
VERSION=1.1.5
URL=https://www.x.org/archive/individual/lib/libXinerama-$VERSION.tar.xz
SHA256=5094d1f0fcc1828cb1696d0d39d9e866ae32520c54d01f618f1a3c1e30c2085c
ARCHIVE_DIR=libXinerama-$VERSION
DEPENDS="libxext"

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --disable-malloc0returnsnull
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
