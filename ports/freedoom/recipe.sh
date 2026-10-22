# Freedoom: Phase 1, game data for Doom engines (free, BSD), https://freedoom.github.io/
# (/usr/share/games/doom/freedoom1.wad, which doomgeneric finds)

NAME=freedoom
VERSION=0.13.0
URL=https://github.com/freedoom/freedoom/releases/download/v$VERSION/freedoom-$VERSION.zip
SHA256=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59
PLAIN=1

build()
{
    # a zip, which tar can't open
    python3 -m zipfile -e freedoom-$VERSION.zip .
    install -D -m 644 freedoom-$VERSION/freedoom1.wad "$DESTDIR/usr/share/games/doom/freedoom1.wad"
    install -D -m 644 freedoom-$VERSION/COPYING.txt "$DESTDIR/usr/share/doc/freedoom/COPYING.txt"
}
