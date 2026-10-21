# xcb-proto, the X protocol in XML, and the Python code that generates
# libxcb from it, https://xcb.freedesktop.org/
# (for libxcb: installed into $STAGING)

NAME=xcb-proto
VERSION=1.17.0
URL=https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-$VERSION.tar.xz
SHA256=2c1bacd2110f4799f74de6ebb714b94cf6f80fb112316b1219480fd22562148c

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr
    make DESTDIR="$STAGING" install
}
