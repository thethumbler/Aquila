export

CWD != realpath --relative-to=$(SRCDIR) .

dirs-y = aqbox/ fbterm/

all: $(dirs-y)

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $(CWD)/$@
	@$(MAKE) -C $@ $(param)

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
