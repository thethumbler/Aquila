# libXdmcp, the X Display Manager Control Protocol, https://www.x.org/
# (only the library, for other ports: installed into $STAGING; TinyX needs
# its header even without XDMCP)

NAME=libxdmcp
VERSION=1.1.5
URL=https://www.x.org/archive/individual/lib/libXdmcp-$VERSION.tar.xz
SHA256=d8a5222828c3adab70adf69a5583f1d32eb5ece04304f7f8392b6a353aa2228c
ARCHIVE_DIR=libXdmcp-$VERSION
DEPENDS="xorgproto"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared --disable-docs
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
