# ncurses, terminal handling library, https://invisible-island.net/ncurses/
# (only the library, for other ports: installed into $STAGING)

NAME=ncurses
VERSION=6.6
URL=https://ftp.gnu.org/gnu/ncurses/ncurses-$VERSION.tar.gz
SHA256=355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11

build()
{
    aquila_config_sub

    # no terminfo database on the system: the descriptions of the usual
    # terminals are built into the library, made by the host's tic
    ./configure --host=i686-aquila --build="$(./config.guess)" --prefix=/usr \
        --without-cxx --without-cxx-binding --without-ada --without-tests \
        --without-manpages --without-progs --without-shared --without-debug \
        --with-normal --enable-widec --disable-stripping --without-pkg-config \
        --disable-database \
        --with-fallbacks=vt100,ansi,linux,xterm,xterm-256color \
        --with-tic-path="$(command -v tic)" --with-infocmp-path="$(command -v infocmp)" \
        --with-build-cc=cc
    make -j"$JOBS"
    make DESTDIR="$STAGING" install.libs install.includes
}
