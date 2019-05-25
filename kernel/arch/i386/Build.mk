dirs-y += boot/
dirs-y += cpu/
dirs-y += earlycon/
dirs-y += mm/
dirs-y += sys/
dirs-y += platform/

elf    += kernel-$(VERSION).$(ARCH)

kernel-$(VERSION).$(ARCH): builtin.o
	@echo "  ELF     " $(CWD)/$@;
	@$(LD) $(LDFLAGS) -T kernel.$(ARCH).ld -o $@
