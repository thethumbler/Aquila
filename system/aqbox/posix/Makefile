include Build.mk

CWD != realpath --relative-to=$(SRCDIR) .

all: builtin.o

builtin.o: $(obj-y) $(dirs-y)
	@$(ECHO) "  SYSLD   " $(CWD)/builtin.o
	@$(SYSLD) $(SYSLDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@$(ECHO) "  MK      " $(CWD)/$@
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@$(ECHO) "  SYSCC   " $(CWD)/$@
	@$(SYSCC) $(SYSCFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	@$(ECHO) "  RM      " $(obj-y) builtin.o
	@$(RM) $(obj-y) builtin.o
