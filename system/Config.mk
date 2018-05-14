CC := $(SRC_DIR)/build-tools/sys/bin/i686-aquila-gcc
CFLAGS := -m32 -I$(SRC_DIR)/build-tools/libc/sysroot/usr/include -march=i586

LD := $(CC)
LDFLAGS := -nostdlib -ffreestanding -Wl,-Ttext=0x1000
LDLIBS := $(SRC_DIR)/build-tools/libc/sysroot/usr/lib/crt0.o -L $(SRC_DIR)/build-tools/libc/sysroot/usr/lib/ -lc -lpthread -lm -lgcc 
