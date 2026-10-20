# X Window System protocol headers, https://www.x.org/
# (headers only, for other ports: installed into $STAGING)

NAME=xorgproto
VERSION=2024.1
URL=https://www.x.org/archive/individual/proto/xorgproto-$VERSION.tar.xz
SHA256=372225fd40815b8423547f5d890c5debc72e88b91088fbfb13158c20495ccb59

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-specs
    make DESTDIR="$STAGING" install
}
