CC := /opt/cross/bin/i686-aquila-gcc
CFLAGS := -m32 -I$(PDIR)/include -I$(PDIR)/../usr/include 

LD := $(CC)
LDFLAGS := -nostdlib -ffreestanding -Wl,-Ttext=0x1000
LDLIBS := $(PDIR)/../usr/lib/crt0.o -lgcc $(PDIR)/../usr/lib/libc.a
