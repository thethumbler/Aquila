## AquilaOS system support, built into libc.a (crt0.o is built separately)
## object names must not clash with newlib's (e.g. no stdio.c)

libc_a_SOURCES += \
	%D%/atfile.c \
	%D%/crypt.c \
	%D%/dirent.c \
	%D%/flockfile.c \
	%D%/group.c \
	%D%/nanosleep.c \
	%D%/pthread.c \
	%D%/realpath.c \
	%D%/shadow.c \
	%D%/syscalls.c \
	%D%/termios.c \
	%D%/ttydev.c
