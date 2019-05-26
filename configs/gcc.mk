CC := i686-aquila-gcc

CFLAGS += \
		-nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter -march=i386 \
		-funsigned-bitfields -fuse-ld=bfd

AS := $(CC)
ASFLAGS := $(CFLAGS)
LD := i686-aquila-ld
LDFLAGS := -nostdlib -melf_i386

SYSCC := i686-aquila-gcc
SYSLD := i686-aquila-ld
