ROOT= initrd
DIRS= bin sbin dev mnt proc tmp root
COPY= etc

AQBOX= aqbox
AQBOX_BIN= cat clear echo env ls mkdir mknod ps pwd sh stat uname unlink
AQBOX_SBIN= login mount kbd getty

# Create initrd CPIO image
initrd.img: all
	cd $(ROOT); find . | cpio -o > ../initrd.img;

all: $(DIRS) $(COPY) $(ROOT)/init fbterm aqbox

$(ROOT):
	mkdir -p $(ROOT)

# Inflate initrd ROOT with required directories
$(DIRS): $(ROOT)
	mkdir -p $(ROOT)/$@

# Copy some files and directories into initrd ROOT
$(COPY): $(ROOT)
	cp -r $@ $(ROOT)/

fbterm: $(ROOT)/bin/fbterm

$(ROOT)/bin/fbterm: $(ROOT)/bin
	cp ../system/fbterm/fbterm  $(ROOT)/bin/

aqbox: $(ROOT)/bin/aqbox $(AQBOX_BIN) $(AQBOX_SBIN)

$(ROOT)/bin/aqbox: $(ROOT)/bin
	cp ../system/aqbox/aqbox  $(ROOT)/bin/

$(AQBOX_BIN): $(ROOT)/bin/aqbox
	ln -fs /bin/aqbox $(ROOT)/bin/$@

$(AQBOX_SBIN): $(ROOT)/bin/aqbox
	ln -fs /bin/aqbox $(ROOT)/sbin/$@

$(ROOT)/init:
	$(MAKE) -C init/
	cp init/init $@

clean:
	$(MAKE) -C init/ clean
	rm -rf $(ROOT) initrd.img

.PHONY: all $(DIRS) clean $(COPY) fbterm aqbox $(AQBOX_BIN) $(AQBOX_SBIN)
