ROOT= initrd
DIRS= bin sbin dev mnt proc tmp root
COPY= etc usr

AQBOX= aqbox
AQBOX_BIN= cat clear echo env ls mkdir mknod ps pwd sh stat uname unlink touch kill bim date
AQBOX_SBIN= login mount kbd getty readmbr reboot

# Create initrd CPIO image
initrd.img: all
	cd $(ROOT); find . | cpio -o > ../initrd.img;

all: $(DIRS) $(COPY) $(ROOT)/init fbterm aqbox $(ROOT)/dev/kmsg #nuklear

$(ROOT):
	$(MKDIR) -p $(ROOT)

# Inflate initrd ROOT with required directories
$(DIRS): $(ROOT)
	$(MKDIR) -p $(ROOT)/$@

# Copy some files and directories into initrd ROOT
$(COPY): $(ROOT)
	$(CP) -r $@ $(ROOT)/

fbterm: $(ROOT)/bin/fbterm

$(ROOT)/bin/fbterm: $(ROOT)/bin
	$(CP) $(SRCDIR)/system/fbterm/fbterm  $(ROOT)/bin/
	$(STRIP) $(ROOT)/bin/fbterm

aqbox: $(ROOT)/bin/aqbox $(AQBOX_BIN) $(AQBOX_SBIN)

$(ROOT)/bin/aqbox: $(ROOT)/bin
	$(CP) $(SRCDIR)/system/aqbox/aqbox $(ROOT)/bin/
	$(STRIP) $(ROOT)/bin/aqbox

$(AQBOX_BIN): $(ROOT)/bin/aqbox
	$(LN) -fs /bin/aqbox $(ROOT)/bin/$@

$(AQBOX_SBIN): $(ROOT)/bin/aqbox
	$(LN) -fs /bin/aqbox $(ROOT)/sbin/$@

$(ROOT)/init:
	$(MAKE) -C init/
	$(CP) init/init $@

$(ROOT)/dev/kmsg:
	#sudo mknod $(ROOT)/dev/kmsg c 1 11

clean:
	$(MAKE) -C init/ clean
	$(RM) -rf $(ROOT) initrd.img

.PHONY: all $(DIRS) clean $(COPY) fbterm aqbox $(AQBOX_BIN) $(AQBOX_SBIN) install
install: all
	$(MKDIR) -p $(DESTDIR)/boot
	$(CP) initrd.img $(DESTDIR)/boot
