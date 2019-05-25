CC := i686-aquila-pcc

CFLAGS := $(INCLUDES) \
		-nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter \
		-funsigned-bitfields

AS := gcc
ASFLAGS := $(CFLAGS)
#LD := ld.gold
LD := ld
LDFLAGS := -nostdlib -melf_i386
