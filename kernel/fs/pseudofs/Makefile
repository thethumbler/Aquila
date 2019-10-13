include Build.mk

CWD != realpath --relative-to=$(SRCDIR) .

all: builtin.o $(elf)

builtin.o: $(obj-y) $(dirs-y)
	@$(ECHO) "  LD      " $(CWD)/builtin.o;
	@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@$(ECHO) "  MK      " $(CWD)/$@;
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@$(ECHO) "  CC      " $(CWD)/$@;
	@$(CC) $(CFLAGS) -c $< -o $@

%.o:%.S
	@$(ECHO) "  AS      " $(CWD)/$@;
	@$(AS) $(ASFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	@$(ECHO) "  RM      " $(obj-y) $(elf) builtin.o
	@$(RM) $(obj-y) $(elf) builtin.o
