# doomgeneric, Doom made easy to port, https://github.com/ozkl/doomgeneric
# (`doom` and `xdoom` in /usr/bin; from git: there are no releases)
#
# doom: on the framebuffer and the virtual terminal's keyboard (its own
# doomgeneric_aquila.c), 320x200 four times over (1280x800, Limine's
# mode), centered on another screen. xdoom: in an X window (Xlib), twice
# over (640x400). The game data is Freedoom's, an IWAD in
# /usr/share/games/doom (or -iwad) is found too. No sound

NAME=doomgeneric
VERSION=e257a09
GIT=https://github.com/ozkl/doomgeneric.git
COMMIT=e257a09527062bfb986b6e5e0780839b6cbd5b71
DEPENDS="freedoom libx11"

build()
{
    cd doomgeneric
    cp "$ROOT/ports/doomgeneric/doomgeneric_aquila.c" .

    # its own sources, the Aquila platform instead of Xlib's
    local objs
    objs=$(sed -n 's/^SRC_DOOM = //p' Makefile | sed 's/doomgeneric_xlib\.o/doomgeneric_aquila.o/')

    make -j"$JOBS" CC="$CC" SRC_DOOM="$objs" LIBS="-lm" LDFLAGS= \
        CFLAGS="-O2 -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=1280 -DDOOMGENERIC_RESY=800"
    install -D -m 755 doomgeneric "$DESTDIR/usr/bin/doom"
    $STRIP "$DESTDIR/usr/bin/doom"

    # the same with its Xlib platform, apart (its own objects)
    objs=$(sed -n 's/^SRC_DOOM = //p' Makefile)
    make -j"$JOBS" CC="$CC" SRC_DOOM="$objs" OBJDIR=build-x OUTPUT=xdoom LDFLAGS= \
        LIBS="$(pkg-config --static --libs x11) -lm" \
        CFLAGS="-O2 -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 $(pkg-config --cflags x11)"
    install -D -m 755 xdoom "$DESTDIR/usr/bin/xdoom"
    $STRIP "$DESTDIR/usr/bin/xdoom"
}
