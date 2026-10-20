# libtermkey, keyboard input for terminal programs (used by vis),
# http://www.leonerd.org.uk/code/libtermkey/ (a library, into $STAGING)

NAME=libtermkey
VERSION=0.22
URL=http://www.leonerd.org.uk/code/libtermkey/libtermkey-$VERSION.tar.gz
SHA256=6945bd3c4aaa83da83d80a045c5563da4edd7d0374c62c0d35aec09eb3014600
DEPENDS=ncurses

build()
{
    # its Makefile builds with libtool, three files don't need it
    local src="termkey.c driver-csi.c driver-ti.c"

    $CC -Os -std=c99 $CPPFLAGS -c $src
    $AR rcs libtermkey.a ${src//.c/.o}

    install -D -m 644 termkey.h "$STAGING/usr/include/termkey.h"
    install -D -m 644 libtermkey.a "$STAGING/usr/lib/libtermkey.a"
}
