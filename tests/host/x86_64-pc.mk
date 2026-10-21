#
# x86_64-pc configuration built with the host gcc/binutils instead of a
# cross toolchain:
#
#   make kernel CONFIG=../tests/host/x86_64-pc
#

include $(SRCDIR)/configs/x86_64-pc.mk

# Distribution compilers default to PIE, stack protector and CET
HOST_CFLAGS := -fno-pic -fno-pie -fno-stack-protector \
               -fcf-protection=none -fno-asynchronous-unwind-tables

CC := gcc
AS := $(CC)
LD := ld

CFLAGS  += $(HOST_CFLAGS)
ASFLAGS += $(HOST_CFLAGS)
