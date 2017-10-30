export

MAKEFLAGS += --no-print-directory

include config

CC := gcc
PDIR != pwd
CFLAGS := -std=gnu99 -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding -m32 \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields -fuse-ld=bfd \
		  -Wno-unused
		  #-fdump-rtl-expand
		  #-no-vec

AS := $(CC)
ASFLAGS := $(CFLAGS)

LD := /usr/bin/ld.bfd
LDFLAGS := -nostdlib -melf_i386

PERL := perl

dirs-y = core/ fs/ devices/ sys/ mm/ arch/

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
