export

MAKEFLAGS += --no-print-directory

include config

dirs-y = aqbox/ fbterm/

all: $(dirs-y)

.PHONY: clean
clean: param = clean
clean: $(dirs-y)

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)
