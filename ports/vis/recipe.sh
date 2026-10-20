# vis, a vi-like editor with structural regular expressions,
# https://github.com/martanne/vis (without Lua: no syntax highlighting)

NAME=vis
VERSION=0.9
URL=https://github.com/martanne/vis/archive/refs/tags/v$VERSION.tar.gz
FILE=vis-$VERSION.tar.gz
SHA256=bd37ffba5535e665c1e883c25ba5f4e3307569b6d392c60f3c7d5dedd2efcfca
DEPENDS="ncurses libtermkey"

build()
{
    CFLAGS="-Os $CPPFLAGS" ./configure --prefix=/usr \
        --disable-lua --disable-tre --disable-selinux --disable-acl

    # the kernel loads static executables, not position independent ones
    sed -i 's/ -fPIE//g; s/ -pie//g' config.mk

    make -j"$JOBS"

    install -d "$DESTDIR/usr/bin"
    install -m 755 vis vis-digraph vis-menu "$DESTDIR/usr/bin/"
}
