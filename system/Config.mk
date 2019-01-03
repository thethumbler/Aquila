#CC := $(SRC_DIR)/build-tools/sys/bin/i686-aquila-gcc
ARCH=i686
#ARCH=x86_64
#CC := $(SRC_DIR)/build-tools/sysroot/bin/$(ARCH)-aquila-gcc
CC := /opt/aquila/bin/$(ARCH)-aquila-gcc
#CFLAGS := -I$(SRC_DIR)/build-tools/sysroot/usr/$(ARCH)-aquila/include

#LD := $(CC)
LD := /opt/aquila/bin/$(ARCH)-aquila-ld
#LDFLAGS := -nostdlib -ffreestanding -Wl,-Ttext=0x1000
#LDLIBS := $(SRC_DIR)/build-tools/sysroot/usr/$(ARCH)-aquila/lib/crt0.o -L $(SRC_DIR)/build-tools/libc/sysroot/usr/$(ARCH)-aquila/lib -lc -lpthread -lm -lgcc 
