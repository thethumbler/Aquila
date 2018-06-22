export

MAKEFLAGS += --no-print-directory

PDIR := $(SRC_DIR)/system/nuklear
CFLAGS += -I$(PDIR)/include
LDFLAGS += -L$(PDIR)/libnk
#LDLIBS += -lnk

include Build.mk

all: builtin.o nuklear apps

.PHONY: apps
apps:
	@echo -e "  MK      " $@
	@$(MAKE) -C apps/ $(param)

nuklear: builtin.o libnk/libnk.a
	@echo -e "  LD      " $@
	@$(LD) $(LDFLAGS) $< -lnk $(LDLIBS) -o $@;

builtin.o: $(obj-y) $(dirs-y)
	@echo -e "  LD      " builtin.o;
	@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; 

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@echo -e "  MK      " $@
	@$(MAKE) -C $@ $(param)

%.o:%.c
	@echo -e "  CC      " $@;
	@$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean: param = clean
clean: $(dirs-y) apps 
	$(RM) -f nuklear builtin.o $(obj-y)
