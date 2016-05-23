export

MAKEFLAGS += --no-print-directory

include config

PDIR != pwd
CFLAGS := -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding -m32 \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields
AS := $(CC)
ASFLAGS := $(CFLAGS)
LDFLAGS := -nostdlib -melf_i386

PERL := perl

dirs = arch/ core/ fs/ devices/ sys/

all: $(dirs)

.PHONY: clean
clean: param = clean
clean: $(dirs)

.PHONY: $(dirs)
$(dirs): $(patsubst %/,%/Makefile, $(dirs))
	$(MAKE) -C $@ $(param)

$(patsubst %/,%/Makefile, $(dirs)): $(patsubst %/,%/Build, $(dirs))
	cd $(dir $@) && $(PERL) $(PDIR)/gen.pl > Makefile && \
	for file in `ls *.[Sc]`; do $(CC) $(CFLAGS) -M $$file >> Makefile; done

.PHONY: distclean
distclean: param = distclean
distclean: $(dirs)
