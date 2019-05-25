#
# Compilation flags
#

INCLUDES := \
	-I. \
	-I$(PDIR)/arch/$$(ARCH_DIR)/platform/$$(PLATFORM_DIR)/include \
	-I$(PDIR)/arch/$$(ARCH_DIR)/include \
	-I$(PDIR)/include \
	-I$(PDIR)
	#-I/opt/aquila/lib/gcc/i686-aquila/7.3.0/include/
