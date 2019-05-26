CC := clang

CFLAGS := $(INCLUDES) \
		-nostdlib -ffreestanding -m32 \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter -march=i386

AS := $(CC)
ASFLAGS := $(CFLAGS)
#LD := ld.gold
LD := ld.lld
LDFLAGS := -nostdlib -melf_i386
