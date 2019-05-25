CC := $(PDIR)/../build-tools/sysroot/bin/i686-elf-gcc

CFLAGS := $(INCLUDES) \
		-nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter -march=i386 \
		-funsigned-bitfields -fuse-ld=bfd

AS := $(CC)
ASFLAGS := $(CFLAGS)
LD := $(PDIR)/../build-tools/sysroot/bin/i686-elf-ld.bfd
LDFLAGS := -nostdlib -melf_i386
