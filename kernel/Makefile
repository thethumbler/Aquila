export

MAKEFLAGS += --no-print-directory

include config.mk

PDIR := $(TRAVIS_BUILD_DIR)/kernel

ifeq ($(PDIR),/kernel)
PDIR != pwd
endif

CC := $(PDIR)/../build-tools/sys/bin/i686-elf-gcc
CFLAGS := -std=gnu99 -I. -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding -m32 \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields -fuse-ld=bfd \
		  #-Wno-unused

AS := $(CC)
ASFLAGS := $(CFLAGS)

LD := $(PDIR)/../build-tools/sys/bin/i686-elf-ld.bfd
LDFLAGS := -nostdlib -melf_i386

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
