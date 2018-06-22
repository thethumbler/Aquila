export

MAKEFLAGS += --no-print-directory

include Build.mk

all: $(dirs-y)

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
