#
# User space programs
#
# Built with the i686-aquila toolchain into $(UBUILD)/<program>, each
# program is every .c file under its directory.
#

UBUILD ?= $(BUILDDIR)/userland
USRC   := $(SRCDIR)/userland

UCC     := i686-aquila-gcc
UCFLAGS := -O2 -std=gnu11 -Wall -Wno-unused -Wno-unused-parameter -MMD -MP
ULDLIBS := -lm

PROGS := aqbox

aqbox_CFLAGS  := -I$(USRC)/aqbox/include

srcs = $(patsubst $(USRC)/%,%,$(shell find $(USRC)/$(1) -name '*.c'))
objs = $(addprefix $(UBUILD)/,$(patsubst %.c,%.o,$(call srcs,$(1))))

.PHONY: all clean
all: $(addprefix $(UBUILD)/bin/,$(PROGS))

# $(call program,name)
define program
$(UBUILD)/bin/$(1): $(call objs,$(1))
	@$(MKDIR) $$(@D)
	@$(ECHO) "  LD      " userland/$(1)
	@$(UCC) $$^ $(ULDLIBS) -o $$@

$(UBUILD)/$(1)/%.o: $(USRC)/$(1)/%.c
	@$(MKDIR) $$(@D)
	@$(ECHO) "  CC      " userland/$(1)/$$*.o
	@$(UCC) $(UCFLAGS) $$($(1)_CFLAGS) -I$$(<D) -c $$< -o $$@

-include $(patsubst %.o,%.d,$(call objs,$(1)))
endef

$(foreach p,$(PROGS),$(eval $(call program,$(p))))

clean:
	@$(RM) -r $(UBUILD)
