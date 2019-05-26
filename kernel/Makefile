export

PDIR := $(SRCDIR)/kernel

#
# Add include pathes to CFLAGS & ASFLAGS
#

CFLAGS += \
	-I. \
	-I$(PDIR)/arch/$(ARCH_DIR)/platform/$(PLATFORM_DIR)/include \
	-I$(PDIR)/arch/$(ARCH_DIR)/include \
	-I$(PDIR)/include \
	-I$(PDIR)

ASFLAGS += \
	-I. \
	-I$(PDIR)/arch/$(ARCH_DIR)/platform/$(PLATFORM_DIR)/include \
	-I$(PDIR)/arch/$(ARCH_DIR)/include \
	-I$(PDIR)/include \
	-I$(PDIR)

dirs-y = core/ fs/ dev/ sys/ mm/ net/

all: $(dirs-y) arch/

.PHONY: $(dirs-y)
$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))
	@$(ECHO) "  MK      " kernel/$@
	@$(MAKE) -C $@ $(param)

.PHONY: arch/
arch/: $(dirs-y) arch/Makefile
	@$(ECHO) "  MK      " kernel/$@
	@$(MAKE) -C $@ $(param)

.PHONY: install
install: all
	@$(ECHO) "Installing kernel-$(VERSION).$(ARCH) into $(DESTDIR)/boot/kernel"
	@$(MKDIR) -p $(DESTDIR)/boot
	@$(CP) arch/$(ARCH)/kernel-$(VERSION).$(ARCH) $(DESTDIR)/boot/kernel

.PHONY: clean
clean: param = clean
clean: $(dirs-y) arch/

