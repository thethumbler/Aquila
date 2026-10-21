# netcat, OpenBSD's nc, https://salsa.debian.org/debian/netcat-openbsd
# (fetched from Debian, whose orig tarball is OpenBSD's source; its first
# two patches, vendored, port it to Linux and build it without TLS)

NAME=netcat
VERSION=1.239
URL=https://deb.debian.org/debian/pool/main/n/netcat-openbsd/netcat-openbsd_$VERSION.orig.tar.gz
SHA256=8a2e52ea411f68c89952c944c32e4e5efbe901a566fb5fc6bbe92844a49b2a8e
ARCHIVE_DIR=netcat-openbsd-$VERSION

build()
{
    make CC="$CC" CFLAGS="-Os -Wno-deprecated-declarations" LDFLAGS="-s"

    install -d "$DESTDIR/bin"
    install -m 755 nc "$DESTDIR/bin/nc"
}
