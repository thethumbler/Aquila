# libXft, anti-aliased text through FreeType and fontconfig, https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxft
VERSION=2.3.8
URL=https://www.x.org/archive/individual/lib/libXft-$VERSION.tar.xz
SHA256=5e8c3c4bc2d4c0a40aef6b4b38ed2fb74301640da29f6528154b5009b1c6dd49
ARCHIVE_DIR=libXft-$VERSION
DEPENDS="libxrender fontconfig freetype"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-malloc0returnsnull
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
