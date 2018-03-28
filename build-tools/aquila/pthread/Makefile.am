AUTOMAKE_OPTIONS = cygnus
INCLUDES = $(NEWLIB_CFLAGS) $(CROSS_CFLAGS) $(TARGET_CFLAGS)

LIB_SOURCES= \
	pthread_create.c \
	pthread_exit.c \
	pthread_join.c

LIBADD_OBJS = \
	$(lpfx)pthread.$(oext)

SUBDIRS = .
tooldir = $(tooldir)
toollibdir = $(top_toollibdir)

toollib_LIBRARIES = libpthread.a
libpthread_a_SOURCES = $(LIB_SOURCES)
libpthread_a_LIBADD = $(LIBADD_OBJS)
libpthread_a_DEPENDENCIES = $(LIBADD_OBJS)
libpthread_a_CFLAGS = $(AM_CFLAGS)
LIB_COMPILE = $(COMPILE)

ACLOCAL_AMFLAGS = -I ../../..
CONFIG_STATUS_DEPENDENCIES = $(newlib_basedir)/configure.host

CFLAGS_pthread=

$(lpfx)pthread.$(oext): $(srcdir)/pthread_create.c
	$(LIB_COMPILE) $(CFLAGS_pthread) -c $< -o $@
