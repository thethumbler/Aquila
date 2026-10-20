## AquilaOS system support, built into libc.a (crt0.o is built separately)
## object names must not clash with newlib's (e.g. no stdio.c)

libc_a_SOURCES += \
	%D%/crypt.c \
	%D%/dirent.c \
	%D%/flockfile.c \
	%D%/pthread.c \
	%D%/shadow.c \
	%D%/syscalls.c \
	%D%/termios.c
