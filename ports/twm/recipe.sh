# twm, the Tab Window Manager of X, https://www.x.org/

NAME=twm
VERSION=1.0.13.1
URL=https://www.x.org/archive/individual/app/twm-$VERSION.tar.xz
SHA256=a52534755aa8b492c884e52fa988bac84ab4d54641954679b9aaf08e323df2c5
DEPENDS="libx11 libxext libxt libxmu libsm libice"

build()
{
    aquila_config_sub

    # its configuration's parser is made by the build host's yacc
    ./configure --host=i686-aquila --prefix=/usr --datadir=/usr/share
    make -j"$JOBS"
    install -D -m 755 src/twm "$DESTDIR/usr/bin/twm"
    $STRIP "$DESTDIR/usr/bin/twm"
    install -D -m 644 src/system.twmrc "$DESTDIR/usr/share/X11/twm/system.twmrc"
}
