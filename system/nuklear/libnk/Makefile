export

MAKEFLAGS += --no-print-directory

PDIR := $(SRC_DIR)/system/nuklear
CFLAGS += -I$(PDIR)/include -Iinclude/

include Build.mk

all: builtin.o libnk.a

libnk.a: builtin.o
	@echo -e "  AR      " $@ $<
	@$(AR) rcs $@ $<

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
	$(RM) -f libnk.a builtin.o $(obj-y)
