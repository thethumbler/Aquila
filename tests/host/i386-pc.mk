#
# i386-pc configuration built with the host gcc/binutils instead of the
# i686-aquila cross toolchain, used by tests/run.sh:
#
#   make kernel CONFIG=../tests/host/i386-pc
#
# Only the kernel can be built this way, the system components need the
# aquila toolchain and newlib.
#

include $(SRCDIR)/configs/i386-pc.mk

# Distribution compilers default to PIE, stack protector and CET
HOST_CFLAGS := -fno-pic -fno-pie -fno-stack-protector \
               -fcf-protection=none -fno-asynchronous-unwind-tables

CC := gcc
AS := $(CC)
LD := ld

CFLAGS  += $(HOST_CFLAGS)
ASFLAGS += $(HOST_CFLAGS)
