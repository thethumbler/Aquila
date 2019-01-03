obj-y  += none.o
elf    += kernel-$(VERSION).$(ARCH)

kernel-$(VERSION).$(ARCH): builtin.o
	@echo -e "  ELF     " $@;
	@$(LD) $(LDFLAGS) -Tkernel.$(ARCH).ld -lgcc -o $@
