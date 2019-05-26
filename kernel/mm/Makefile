include Build.mk

CWD != realpath --relative-to=$(SRCDIR) .

all: builtin.o $(elf)

builtin.o: $(obj-y) $(dirs-y)
	@$(ECHO) "  LD      " kernel/$(CWD)/builtin.o;
	@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@$(ECHO) "  MK      " kernel/$(CWD)/$@;
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@$(ECHO) "  CC      " kernel/$(CWD)/$@;
	@$(CC) $(CFLAGS) -c $< -o $@

%.o:%.S
	@$(ECHO) "  AS      " kernel/$(CWD)/$@;
	@$(AS) $(ASFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	@$(ECHO) "  RM      " $(obj-y) $(elf) builtin.o
	@$(RM) $(obj-y) $(elf) builtin.o
