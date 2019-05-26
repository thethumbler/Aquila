export

PDIR := $(SRCDIR)/system/fbterm
CWD != realpath --relative-to=$(SRCDIR) .

SYSCFLAGS += -I$(PDIR)/include
SYSLDLIBS += -lpthread

include Build.mk

all: builtin.o fbterm

fbterm: builtin.o
	@$(ECHO) "  ELF     " $(CWD)/$@;
	@$(SYSCC) $(SYSLDFLAGS) $< $(SYSLDLIBS) -o $@;

builtin.o: $(obj-y) $(dirs-y)
	@$(ECHO) "  SYSLD   " $(CWD)/builtin.o;
	@$(SYSLD) $(SYSLDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@$(ECHO) "  MK      " $(CWD)/$@
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@$(ECHO) "  SYSCC   " $(CWD)/$@;
	@$(SYSCC) $(SYSCFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	@$(ECHO) "  RM      " $(obj-y) builtin.o fbterm
	@$(RM) -f fbterm builtin.o $(obj-y)
