obj-$(X86_MULTIBOOT) += multiboot.o
obj-$(ARCH_X86_64)   += x86_64_bootstrap.o
obj-$(ARCH_I386)     += init.o
obj-$(ARCH_I386)     += sys.o
