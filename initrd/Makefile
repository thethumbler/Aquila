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
	mkdir -p $(ROOT)

# Inflate initrd ROOT with required directories
$(DIRS): $(ROOT)
	mkdir -p $(ROOT)/$@

# Copy some files and directories into initrd ROOT
$(COPY): $(ROOT)
	cp -r $@ $(ROOT)/

fbterm: $(ROOT)/bin/fbterm
vterm: $(ROOT)/bin/vterm

kilo: $(ROOT)/bin/kilo

$(ROOT)/bin/kilo: $(ROOT)/bin
	cp ../system/kilo/kilo  $(ROOT)/bin/
	strip $(ROOT)/bin/kilo

nuklear: $(ROOT)/bin/nuklear

$(ROOT)/bin/nuklear: $(ROOT)/bin
	cp ../system/nuklear/nuklear  $(ROOT)/bin/
	strip $(ROOT)/bin/nuklear

$(ROOT)/bin/fbterm: $(ROOT)/bin
	cp ../system/fbterm/fbterm  $(ROOT)/bin/
	strip $(ROOT)/bin/fbterm

$(ROOT)/bin/vterm: $(ROOT)/bin
	cp ../system/vterm/vterm  $(ROOT)/bin/
	strip $(ROOT)/bin/vterm

aqbox: $(ROOT)/bin/aqbox $(AQBOX_BIN) $(AQBOX_SBIN)

$(ROOT)/bin/aqbox: $(ROOT)/bin
	cp ../system/aqbox/aqbox  $(ROOT)/bin/
	strip $(ROOT)/bin/aqbox

$(AQBOX_BIN): $(ROOT)/bin/aqbox
	ln -fs /bin/aqbox $(ROOT)/bin/$@

$(AQBOX_SBIN): $(ROOT)/bin/aqbox
	ln -fs /bin/aqbox $(ROOT)/sbin/$@

$(ROOT)/init:
	$(MAKE) -C init/
	cp init/init $@

$(ROOT)/dev/kmsg:
	#sudo mknod $(ROOT)/dev/kmsg c 1 11

clean:
	$(MAKE) -C init/ clean
	rm -rf $(ROOT) initrd.img

.PHONY: all $(DIRS) clean $(COPY) fbterm aqbox $(AQBOX_BIN) $(AQBOX_SBIN)
