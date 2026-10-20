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
    # Latin ones, not the Japanese and Korean (megabytes each)
    install -d "$dir"
    for f in [0-9]*x[0-9]*.bdf; do
        case $f in *ja.bdf|*ko.bdf) continue ;; esac
        gzip -9n < "$f" > "$dir/$f.gz"
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

    # the usual names, of the ISO 10646 fonts (X.Org's are ISO 8859-1)
    local alias name xlfd
    while read -r name xlfd; do
        grep -q -i -F -- " $xlfd" "$dir/fonts.dir" && printf '%-12s %s\n' "$name" "$xlfd"
    done > "$dir/fonts.alias" <<'ALIASES'
fixed -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1
5x7 -misc-fixed-medium-r-normal--7-70-75-75-c-50-iso10646-1
5x8 -misc-fixed-medium-r-normal--8-80-75-75-c-50-iso10646-1
6x9 -misc-fixed-medium-r-normal--9-90-75-75-c-60-iso10646-1
6x10 -misc-fixed-medium-r-normal--10-100-75-75-c-60-iso10646-1
6x12 -misc-fixed-medium-r-semicondensed--12-110-75-75-c-60-iso10646-1
6x13 -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1
6x13bold -misc-fixed-bold-r-semicondensed--13-120-75-75-c-60-iso10646-1
7x13 -misc-fixed-medium-r-normal--13-120-75-75-c-70-iso10646-1
7x13bold -misc-fixed-bold-r-normal--13-120-75-75-c-70-iso10646-1
7x14 -misc-fixed-medium-r-normal--14-130-75-75-c-70-iso10646-1
7x14bold -misc-fixed-bold-r-normal--14-130-75-75-c-70-iso10646-1
8x13 -misc-fixed-medium-r-normal--13-120-75-75-c-80-iso10646-1
8x13bold -misc-fixed-bold-r-normal--13-120-75-75-c-80-iso10646-1
9x15 -misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1
9x15bold -misc-fixed-bold-r-normal--15-140-75-75-c-90-iso10646-1
9x18 -misc-fixed-medium-r-normal--18-120-100-100-c-90-iso10646-1
9x18bold -misc-fixed-bold-r-normal--18-120-100-100-c-90-iso10646-1
10x20 -misc-fixed-medium-r-normal--20-200-75-75-c-100-iso10646-1
ALIASES
}
