CC := i686-aquila-gcc

CFLAGS += \
		-std=gnu11 -nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter -march=i686 \
		-fno-omit-frame-pointer \
		-funsigned-bitfields -fuse-ld=bfd

AS := $(CC)
ASFLAGS := $(CFLAGS)
LD := i686-aquila-ld
# the kernel is mapped as a single RWX segment
LDFLAGS := -nostdlib -melf_i386 --no-warn-rwx-segments

