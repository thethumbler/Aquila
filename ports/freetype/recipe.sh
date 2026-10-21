# FreeType, the font rasterizer (TrueType, ...), https://freetype.org/
# (only the library, for other ports: installed into $STAGING)

NAME=freetype
VERSION=2.13.3
URL=https://downloads.sourceforge.net/project/freetype/freetype2/$VERSION/freetype-$VERSION.tar.xz
SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
DEPENDS="zlib"

build()
{
    aquila_config_sub

    # compressed fonts with zlib, no PNG, bzip2, Brotli or HarfBuzz
    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --with-zlib=yes --with-bzip2=no --with-png=no --with-harfbuzz=no \
        --with-brotli=no
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
