# GNU Bash, the Bourne Again SHell, https://www.gnu.org/software/bash/
# (`bash` in /bin; 5.3 with its official patches, from git: patch 20)
#
# Its own readline over ncurses; the C library's malloc, not bash's;
# what configure can't run when cross-compiling, said for it

NAME=bash
VERSION=5.3.20
GIT=https://git.savannah.gnu.org/git/bash.git
COMMIT=9c465866b7d849378369ef700cbe2965aa9691e3
DEPENDS="ncurses"

build()
{
    aquila_config_sub

    # AquilaOS has job control, named pipes, sigsetjmp, /dev/fd isn't
    bash_cv_job_control_missing=present \
    bash_cv_sys_named_pipes=present \
    bash_cv_func_sigsetjmp=present \
    bash_cv_getcwd_malloc=yes \
    bash_cv_func_strcoll_broken=no \
    bash_cv_unusable_rtsigs=no \
    bash_cv_printf_a_format=yes \
    bash_cv_dev_fd=absent \
    bash_cv_termcap_lib=libncursesw \
    ./configure --host=$TARGET --build="$(./support/config.guess)" --prefix=/usr \
        --without-bash-malloc --disable-nls --with-curses
    make -j"$JOBS"
    install -D -m 755 bash "$DESTDIR/bin/bash"
    $STRIP "$DESTDIR/bin/bash"
}
