export

MAKEFLAGS += --no-print-directory

PDIR := $(SRC_DIR)/system/fbterm
CFLAGS += -I$(PDIR)/include

include Build.mk

all: builtin.o fbterm

fbterm: builtin.o
	$(LD) $(LDFLAGS) $< $(LDLIBS) -o $@;

builtin.o: $(obj-y) $(dirs-y)
	@echo -e "  LD      " builtin.o;
	@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@echo -e "  CC      " $@;
	@ $(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y)
	$(RM) -f fbterm builtin.o $(obj-y)
