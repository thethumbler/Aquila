dirs-y += boot/
dirs-y += cpu/
dirs-y += earlycon/
dirs-y += mm/
dirs-y += sys/
dirs-y += chipset/

elf += kernel.elf

ifeq ($(ARCH),X86)
kernel.elf: builtin.o
	@echo -e "  ELF     " $@;
	@$(CC) $(CFLAGS) -Wl,-Tlink.ld,-melf_i386 -lgcc -o $@
else
kernel.elf: builtin.o
	@echo -e "  ELF     " $@;
	@$(CC) $(CFLAGS) -Wl,-Tlink64.ld -lgcc -o $@
endif
