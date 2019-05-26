CC := /opt/aquila/bin/i686-aquila-pcc

CFLAGS += \
		-nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter -march=i386 \
		-funsigned-bitfields -fuse-ld=bfd

AS := $(CC)
ASFLAGS := $(CFLAGS)
LD := /opt/aquila/bin/i686-aquila-ld
LDFLAGS := -nostdlib -melf_i386

SYSCC := /opt/aquila/bin/i686-aquila-pcc
SYSLD := /opt/aquila/bin/i686-aquila-ld
