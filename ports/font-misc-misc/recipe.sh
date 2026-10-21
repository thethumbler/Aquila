# The fixed width fonts of X (fixed, 6x13, 9x15, ...), Unicode (ISO 10646),
# https://www.x.org/

NAME=font-misc-misc
VERSION=1.1.3
URL=https://www.x.org/archive/individual/font/font-misc-misc-$VERSION.tar.xz
SHA256=79abe361f58bb21ade9f565898e486300ce1cc621d5285bec26e14b6a8618fed
DEPENDS="font-cursor-misc"

build()
{
    local dir=$DESTDIR/usr/share/fonts/X11/misc f

    # BDF, which the server reads as is (there is no bdftopcf here); the
    # Latin ones, not the Japanese and Korean (megabytes each). Each as it
    # is (Unicode), and its first 256 characters, ISO 8859-1, what clients
    # use by default (fixed): the metrics of a font go to a client whole,
    # 65536 characters are hundreds of KB (ucs2any does this with X.Org)
    install -d "$dir"
    for f in [0-9]*x[0-9]*.bdf; do
        case $f in *ja.bdf|*ko.bdf) continue ;; esac
        gzip -9n < "$f" > "$dir/$f.gz"
        awk '
            /^FONT / { sub(/ISO10646-1$/, "ISO8859-1") }
            /^CHARSET_REGISTRY / { $0 = "CHARSET_REGISTRY \"ISO8859\"" }
            /^CHARS / { header = 0; next }
            /^STARTCHAR / { inchar = 1; glyph = "" }
            /^ENCODING / { enc = $2 + 0 }
            inchar { glyph = glyph $0 "\n"; if (/^ENDCHAR/) { inchar = 0; if (enc >= 0 && enc < 256) { glyphs = glyphs glyph; n++ } } ; next }
            /^ENDFONT/ { printf "CHARS %d\n%s", n, glyphs; print; next }
            { print }
        ' "$f" | gzip -9n > "$dir/${f%.bdf}-8859-1.bdf.gz"
    done

    # the fonts of the directory and their names (mkfontdir), cursor too
    (
        cd "$dir"
        set -- *.bdf.gz
        echo $#
        for f; do
            echo "$f $(gzip -dc "$f" | sed -n 's/^FONT //p' | head -1)"
        done
    ) > "$dir/fonts.dir"

    # the usual names, of the ISO 8859-1 fonts as X.Org's
    local alias name xlfd
    while read -r name xlfd; do
        grep -q -i -F -- " $xlfd" "$dir/fonts.dir" && printf '%-12s %s\n' "$name" "$xlfd"
    done > "$dir/fonts.alias" <<'ALIASES'
fixed -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
5x7 -misc-fixed-medium-r-normal--7-70-75-75-c-50-iso8859-1
5x8 -misc-fixed-medium-r-normal--8-80-75-75-c-50-iso8859-1
6x9 -misc-fixed-medium-r-normal--9-90-75-75-c-60-iso8859-1
6x10 -misc-fixed-medium-r-normal--10-100-75-75-c-60-iso8859-1
6x12 -misc-fixed-medium-r-semicondensed--12-110-75-75-c-60-iso8859-1
6x13 -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
6x13bold -misc-fixed-bold-r-semicondensed--13-120-75-75-c-60-iso8859-1
7x13 -misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1
7x13bold -misc-fixed-bold-r-normal--13-120-75-75-c-70-iso8859-1
7x14 -misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1
7x14bold -misc-fixed-bold-r-normal--14-130-75-75-c-70-iso8859-1
8x13 -misc-fixed-medium-r-normal--13-120-75-75-c-80-iso8859-1
8x13bold -misc-fixed-bold-r-normal--13-120-75-75-c-80-iso8859-1
9x15 -misc-fixed-medium-r-normal--15-140-75-75-c-90-iso8859-1
9x15bold -misc-fixed-bold-r-normal--15-140-75-75-c-90-iso8859-1
9x18 -misc-fixed-medium-r-normal--18-120-100-100-c-90-iso8859-1
9x18bold -misc-fixed-bold-r-normal--18-120-100-100-c-90-iso8859-1
10x20 -misc-fixed-medium-r-normal--20-200-75-75-c-100-iso8859-1
ALIASES
}
