# sbase, suckless POSIX utilities, https://core.suckless.org/sbase/
# (from git: the 0.1 release archive lacks the scripts/ the build uses)

NAME=sbase
VERSION=0.1
GIT=https://git.suckless.org/sbase
COMMIT=eb749d1c88c0557ed06733de89efda1e252facc2

build()
{
    # these need what AquilaOS doesn't have yet: chroot (root directory),
    # cron and logger (syslog), flock (file locks), nice and renice
    # (priorities), tftp (IP networking)
    local skip="chroot cron flock logger nice renice tftp"
    local bin tool

    rm -f $(printf '%s.c ' $skip)
    bin=$(sed -n '/^BIN =/,/^$/p' Makefile | tr -d '\\\t' | tail -n +2 |
          grep -v -x -F "$(printf '%s\n' $skip)")

    # one binary for all the tools
    make -j"$JOBS" CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="-Os" \
        BIN="$(echo $bin)" sbase-box

    install -d "$DESTDIR/bin"
    install -m 755 sbase-box "$DESTDIR/bin/sbase-box"

    # sbase-box -i would make the links, but it can't run here
    for tool in install '[' $(grep -l '^main' *.c | sed 's/\.c$//'); do
        ln -sf sbase-box "$DESTDIR/bin/$tool"
    done
}
