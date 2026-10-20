# libpthread-stubs, which says where the pthread functions are: in the
# C library here, https://xcb.freedesktop.org/
# (pthread-stubs.pc only, for libxcb: installed into $STAGING)

NAME=libpthread-stubs
VERSION=0.5
URL=https://xorg.freedesktop.org/archive/individual/lib/libpthread-stubs-$VERSION.tar.xz
SHA256=59da566decceba7c2a7970a4a03b48d9905f1262ff94410a649224e33d2442bc

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr
    make DESTDIR="$STAGING" install
}
