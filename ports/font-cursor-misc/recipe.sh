# The cursor font of X servers, https://www.x.org/
# (font-misc-misc lists it in fonts.dir)

NAME=font-cursor-misc
VERSION=1.0.4
URL=https://www.x.org/archive/individual/font/font-cursor-misc-$VERSION.tar.xz
SHA256=25d9c9595013cb8ca08420509993a6434c917e53ca1fec3f63acd45a19d4f982

build()
{
    # BDF, which the server reads as is (there is no bdftopcf here)
    install -d "$DESTDIR/usr/share/fonts/X11/misc"
    gzip -9n < cursor.bdf > "$DESTDIR/usr/share/fonts/X11/misc/cursor.bdf.gz"
}
