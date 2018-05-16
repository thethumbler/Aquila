my $dir_name = $ENV{'PWD'};
$dir_name =~ s/\S*\///g;

print "include Build.mk\n";
print 'all: builtin.o $(elf)', "\n";

print 'builtin.o: $(obj-y) $(dirs-y)', "\n";
print "\t", '@echo -e "  LD      " ', "builtin.o;\n\t",
		'@$(LD) $(LDFLAGS) -r $(obj-y) $(patsubst %/,%/builtin.o, $(dirs-y)) -o builtin.o; ', "\n";

print '.PHONY: $(dirs-y)', "\n";
print '$(dirs-y): $(patsubst %/,%/Makefile, $(dirs-y))', "\n";
print "\t", '@echo -e "  MK      " $@', "\n\t", '@$(MAKE) -C $@ $(param)', "\n";

print '$(patsubst %/,%/Makefile, $(dirs-y)): $(patsubst %/,%/Build.mk, $(dirs-y))', "\n";
print "\t", '@echo -e "  PL      " Makefile', "\n\t", '@cd $(dir $@) && $(PERL) $(PDIR)/scripts/gen.pl > Makefile', "\n";

print "%.o:%.c\n", "\t", '@echo -e "  CC      " $@;', "\n\t", '@ $(CC) $(CFLAGS) -c $< -o $@', "\n";
print "%.o:%.S\n", "\t", '@echo -e "  AS      " $@;', "\n\t", '@ $(AS) $(ASFLAGS) -c $< -o $@', "\n";
print '%.elf: builtin.o', "\n\t",  '@echo -e "  ELF     " $@;', "\n\t", '@ $(CC) $(CFLAGS) -Wl,-Tlink.ld -lgcc -o $@', "\n";

print '.PHONY: clean', "\n", 'clean: param = clean', "\n", 'clean: $(dirs-y)', "\n\t",
	'$(RM) $(obj-y) $(elf)', " builtin.o\n";

print ".PHONY: distclean\n", "distclean: param = distclean\n", 'distclean: $(dirs-y) clean', "\n\t",
	'$(RM) Makefile', "\n";
