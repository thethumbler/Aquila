export

CWD != realpath --relative-to=$(SRCDIR) .
PDIR = $(SRCDIR)/system/aqbox
SYSCFLAGS += -I$(PDIR)/include

include Build.mk

all: builtin.o aqbox

aqbox: builtin.o aqbox.o
	@$(ECHO) "  ELF     " $(CWD)/$@
	@$(SYSCC) $(SYSLDFLAGS) -lm $< $(SYSLDLIBS) -o $@

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
	@$(ECHO) "  RM      " $(obj-y) builtin.o aqbox
	@$(RM) $(obj-y) builtin.o aqbox
