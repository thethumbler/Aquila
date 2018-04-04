export

MAKEFLAGS += --no-print-directory

PDIR := $(SRC_DIR)/kernel

ifeq ($(CONFIG),)
$(error Config file must be specified, e.g. 'make CONFIG=i686-pc')
endif

include configs/$(CONFIG).mk

PERL := perl

dirs-y = core/ fs/ dev/ sys/ mm/ arch/

all: $(dirs-y)

.PHONY: clean
clean: param = clean
clean: $(dirs-y)

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)

$(patsubst %/,%/Makefile, $(dirs-y)): $(patsubst %/,%/Build.mk, $(dirs-y))
	@echo -e "  PL      " $<
	@cd $(dir $@) && $(PERL) $(PDIR)/scripts/gen.pl >> Makefile
	

.PHONY: distclean
distclean: param = distclean
distclean: $(dirs-y)
