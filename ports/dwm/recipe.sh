# dwm, the dynamic window manager of suckless, https://dwm.suckless.org/
# (its default configuration: Mod1 is Alt, Alt+Shift+Enter opens st,
# Alt+p dmenu)

NAME=dwm
VERSION=6.5
URL=https://dl.suckless.org/dwm/dwm-$VERSION.tar.gz
SHA256=21d79ebfa9f2fb93141836c2666cb81f4784c69d64e7f1b2352f9b970ba09729
DEPENDS="libx11 libxft libxinerama font-dejavu"

build()
{
    # the libraries are static: what they need comes after them
    make CC="$CC" \
        INCS="$($PKG_CONFIG --cflags x11 xft xinerama)" \
        LIBS="$($PKG_CONFIG --libs x11 xinerama xft)"
    install -D -m 755 dwm "$DESTDIR/usr/bin/dwm"
    $STRIP "$DESTDIR/usr/bin/dwm"
}
