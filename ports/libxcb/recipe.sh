# libxcb, the C binding of the X protocol, under Xlib,
# https://xcb.freedesktop.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxcb
VERSION=1.17.0
URL=https://xorg.freedesktop.org/archive/individual/lib/libxcb-$VERSION.tar.xz
SHA256=599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55
DEPENDS="xcb-proto xorgproto libpthread-stubs libxau libxdmcp"

build()
{
    aquila_config_sub

    # the code is generated on the build host, from xcb-proto's XML (in
    # $STAGING) by its Python code
    local pythondir
    pythondir=$(echo "$STAGING"/usr/lib/python3*/site-packages)

    PYTHONPATH=$pythondir ./configure --host=i686-aquila --prefix=/usr \
        --disable-shared --disable-devel-docs \
        XCBPROTO_XCBINCLUDEDIR="$STAGING/usr/share/xcb" \
        XCBPROTO_XCBPYTHONDIR="$pythondir"
    PYTHONPATH=$pythondir make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
