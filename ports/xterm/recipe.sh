# xterm, the terminal emulator of X, https://invisible-island.net/xterm/

NAME=xterm
VERSION=401
URL=https://invisible-island.net/archives/xterm/xterm-$VERSION.tgz
SHA256=3da2b5e64cb49b03aa13057d85e62e1f2e64f7c744719c00d338d11cd3e6ca1a
DEPENDS="libxaw ncurses"

build()
{
    aquila_config_sub

    # the bitmap fonts of the server (no FreeType), no setuid, utmp or
    # luit; terminal capabilities from ncurses (static, its tgetent()); the
    # libraries are static, what they need comes after them
    LIBS="$($PKG_CONFIG --libs xaw7 xmu xt xpm xext x11) -lncursesw" \
    ./configure --host=i686-aquila --prefix=/usr \
        --with-app-defaults=/usr/share/X11/app-defaults \
        --disable-freetype --disable-setuid --disable-setgid --without-utempter \
        --disable-luit --disable-full-tgetent --without-xinerama \
        --with-tty-group=no
    make -j"$JOBS"
    install -D -m 755 xterm "$DESTDIR/usr/bin/xterm"
    $STRIP "$DESTDIR/usr/bin/xterm"
    install -D -m 644 XTerm.ad "$DESTDIR/usr/share/X11/app-defaults/XTerm"
}
