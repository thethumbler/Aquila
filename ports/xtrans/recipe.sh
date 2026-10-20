# xtrans, the transport code (sockets) of X servers and clients, built
# into them, https://www.x.org/
# (headers only, for other ports: installed into $STAGING)

NAME=xtrans
VERSION=1.5.2
URL=https://www.x.org/archive/individual/lib/xtrans-$VERSION.tar.xz
SHA256=5c5cbfe34764a9131d048f03c31c19e57fb4c682d67713eab6a65541b4dff86c

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-docs
    make DESTDIR="$STAGING" install
}
