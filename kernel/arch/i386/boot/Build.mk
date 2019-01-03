obj-$(X86_MULTIBOOT) += multiboot.o
obj-$(ARCH_X86_64)   += x86_64_bootstrap.o
obj-y += init.o
obj-y += sys.o
