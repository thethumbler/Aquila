ARCH=i686
CC := $(SRC_DIR)/build-tools/sysroot/bin/$(ARCH)-aquila-gcc
CFLAGS := -I$(SRC_DIR)/build-tools/sysroot/usr/$(ARCH)-aquila/include -D__aquila__
LD := $(CC) #/opt/aquila/bin/$(ARCH)-aquila-ld
LDFLAGS := -nostdlib -ffreestanding -Wl,-Ttext=0x1000
LDLIBS := $(SRC_DIR)/build-tools/sysroot/usr/$(ARCH)-aquila/lib/crt0.o -L $(SRC_DIR)/build-tools/sysroot/usr/$(ARCH)-aquila/lib -lc -lpthread -lm -lgcc 
