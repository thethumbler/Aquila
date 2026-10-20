# kilo, a small text editor, https://github.com/antirez/kilo
# (no releases, pinned to a commit)

NAME=kilo
VERSION=323d93b29bd89a2cb446de90c4ed4fea1764176e
URL=https://github.com/antirez/kilo/archive/$VERSION.tar.gz
FILE=kilo-$VERSION.tar.gz
SHA256=79e62c23fdf369fa0cbbeaab9fd8ae2848dcc0156faf6efc89ea274528b39723

build()
{
    # kilo only asks for POSIX.1-2008 declarations on Linux
    $CC -Os -Wall -std=c99 -D_POSIX_C_SOURCE=200809L -o kilo kilo.c

    install -d "$DESTDIR/usr/bin"
    install -m 755 kilo "$DESTDIR/usr/bin/"
}
