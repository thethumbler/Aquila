# dmenu, the menu of suckless (dmenu_run starts a program), for dwm,
# https://tools.suckless.org/dmenu/

NAME=dmenu
VERSION=5.3
URL=https://dl.suckless.org/tools/dmenu-$VERSION.tar.gz
SHA256=1a8f53e6fd2d749839ec870c5e27b3e14da5c3eeacbfcb945d159e1d5433795f
DEPENDS="libx11 libxft libxinerama font-dejavu"

build()
{
    make CC="$CC" \
        INCS="$($PKG_CONFIG --cflags x11 xft xinerama)" \
        LIBS="$($PKG_CONFIG --libs x11 xinerama xft)"

    local f
    for f in dmenu stest; do
        install -D -m 755 "$f" "$DESTDIR/usr/bin/$f"
        $STRIP "$DESTDIR/usr/bin/$f"
    done
    install -m 755 dmenu_run dmenu_path "$DESTDIR/usr/bin/"
}
