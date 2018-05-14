export

MAKEFLAGS += --no-print-directory

PDIR := $(SRC_DIR)/system/nuklear
CFLAGS += -I$(PDIR)/include
LDFLAGS += -L $(PDIR)
#LDLIBS += -lnk

include Build.mk

all: builtin.o nuklear

nuklear: builtin.o libnk.a
	$(LD) $(LDFLAGS) $< -lnk $(LDLIBS) -o $@;

libnk.a: libnk.o
	$(AR) rcs $@ $<

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
	$(RM) -f libnk.o libnk.a nuklear builtin.o $(obj-y)
