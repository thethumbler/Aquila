dirs-y += boot/
dirs-y += cpu/
dirs-y += earlycon/
dirs-y += mm/
dirs-y += sys/
dirs-y += platform/

elf    += kernel-$(VERSION).$(ARCH)

ifeq ($(ARCH),i386)
kernel-$(VERSION).$(ARCH): builtin.o
	@echo -e "  ELF     " $@;
	@$(CC) $(CFLAGS) -Wl,-Tkernel.$(ARCH).ld -lgcc -o $@
else
kernel-$(VERSION).$(ARCH): builtin.o
	@echo -e "  ELF     " $@;
	@$(CC) $(CFLAGS) -Wl,-Tkernel.$(ARCH).ld -lgcc -o $@
endif
