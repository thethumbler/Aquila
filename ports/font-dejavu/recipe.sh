# DejaVu fonts, TrueType (Sans and Sans Mono, regular and bold): what
# fontconfig finds for sans-serif and monospace (Xft programs: dwm, st),
# https://dejavu-fonts.github.io/

NAME=font-dejavu
VERSION=2.37
URL=https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-$VERSION.tar.bz2
SHA256=fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
ARCHIVE_DIR=dejavu-fonts-ttf-$VERSION

build()
{
    local f
    install -d "$DESTDIR/usr/share/fonts/dejavu"
    for f in DejaVuSans DejaVuSans-Bold DejaVuSansMono DejaVuSansMono-Bold; do
        install -m 644 "ttf/$f.ttf" "$DESTDIR/usr/share/fonts/dejavu/"
    done
}
