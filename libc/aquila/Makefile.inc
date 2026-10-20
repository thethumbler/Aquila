## AquilaOS system support, built into libc.a (crt0.o is built separately)

libc_a_SOURCES += \
	%D%/dirent.c \
	%D%/pthread.c \
	%D%/syscalls.c \
	%D%/termios.c
