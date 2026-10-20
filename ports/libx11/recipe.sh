# libX11, Xlib: the C library of X clients, https://www.x.org/
# (the library and headers for other ports into $STAGING; its data,
# /usr/share/X11 (error messages, locales), on the system)

NAME=libx11
VERSION=1.8.12
URL=https://www.x.org/archive/individual/lib/libX11-$VERSION.tar.xz
SHA256=fa026f9bb0124f4d6c808f9aef4057aad65e7b35d8ff43951cef0abe06bb9a9a
ARCHIVE_DIR=libX11-$VERSION
DEPENDS="xorgproto xtrans libxcb"

build()
{
    aquila_config_sub

    # makekeys runs on the build host, on the protocol's keysymdef.h
    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-specs --without-xmlto --without-fop \
        --disable-loadable-i18n --disable-loadable-xcursor \
        --with-keysymdefdir="$STAGING/usr/include/X11"
    make -j"$JOBS"
    make DESTDIR="$STAGING" install

    install -d "$DESTDIR/usr/share/X11"
    cp -r "$STAGING/usr/share/X11/." "$DESTDIR/usr/share/X11/"
}
