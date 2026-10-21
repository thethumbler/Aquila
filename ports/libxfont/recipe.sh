# libXfont 1, the font library of X servers (TinyX predates libXfont2),
# https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxfont
VERSION=1.5.4
URL=https://www.x.org/archive/individual/lib/libXfont-$VERSION.tar.bz2
SHA256=1a7f7490774c87f2052d146d1e0e64518d32e6848184a18654e8d0bb57883242
ARCHIVE_DIR=libXfont-$VERSION
DEPENDS="zlib xorgproto xtrans libfontenc"

build()
{
    aquila_config_sub

    # bitmap fonts (PCF, BDF) only: no FreeType, no font servers
    ./configure --host=$TARGET --prefix=/usr --disable-shared \
        --disable-freetype --disable-fc --without-bzip2 --disable-devel-docs
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
