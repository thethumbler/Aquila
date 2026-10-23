#
# User space programs
#
# Built with the system's toolchain ($(TARGET)) into $(UBUILD)/<program>, each
# program is every .c file under its directory.
#

UBUILD ?= $(BUILDDIR)/userland
USRC   := $(SRCDIR)/userland

UCC     := $(TARGET)-gcc
UCFLAGS := -O2 -std=gnu11 -Wall -Wno-unused -Wno-unused-parameter -MMD -MP
ULDLIBS := -lm

PROGS := aqbox

aqbox_CFLAGS  := -I$(USRC)/aqbox/include

# rebuilt when libc changes (its headers: a struct's layout, the ABI)
LIBC := $(wildcard $(SYSROOT)/usr/lib/libc.a)

srcs = $(patsubst $(USRC)/%,%,$(shell find $(USRC)/$(1) -name '*.c'))
objs = $(addprefix $(UBUILD)/,$(patsubst %.c,%.o,$(call srcs,$(1))))

.PHONY: all clean
all: $(addprefix $(UBUILD)/bin/,$(PROGS))

# $(call program,name)
define program
$(UBUILD)/bin/$(1): $(call objs,$(1)) $(LIBC)
	@$(MKDIR) $$(@D)
	@$(ECHO) "  LD      " userland/$(1)
	@$(UCC) $$(filter %.o,$$^) $(ULDLIBS) -o $$@

$(UBUILD)/$(1)/%.o: $(USRC)/$(1)/%.c $(LIBC)
	@$(MKDIR) $$(@D)
	@$(ECHO) "  CC      " userland/$(1)/$$*.o
	@$(UCC) $(UCFLAGS) $$($(1)_CFLAGS) -I$$(<D) -c $$< -o $$@

-include $(patsubst %.o,%.d,$(call objs,$(1)))
endef

$(foreach p,$(PROGS),$(eval $(call program,$(p))))

clean:
	@$(RM) -r $(UBUILD)
