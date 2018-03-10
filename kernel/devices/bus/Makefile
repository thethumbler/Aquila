include Build.mk
all: builtin.o $(elf)
builtin.o: $(obj-y) $(dirs-y)
	@echo -e "  LD      " builtin.o;
	@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 
.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)
$(patsubst %/,%/Makefile, $(dirs-y)): $(patsubst %/,%/Build.mk, $(dirs-y))
	@echo -e "  PL      " Makefile
	@cd $(dir $@) && $(PERL) $(PDIR)/scripts/gen.pl > Makefile
%.o:%.c
	@echo -e "  CC      " $@;
	@ $(CC) $(CFLAGS) -c $< -o $@
%.o:%.S
	@echo -e "  AS      " $@;
	@ $(AS) $(ASFLAGS) -c $< -o $@
%.elf:
	@echo -e "  ELF     " $@;
	@ $(CC) $(CFLAGS) -Wl,-Tlink.ld -lgcc -o $@
.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	$(RM) $(obj-y) $(elf) builtin.o
.PHONY: distclean
distclean: param = distclean
distclean: $(dirs-y) clean
	$(RM) Makefile
