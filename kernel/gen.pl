open(my $Build, "<", "Build") or die "Can't open `Build': $!";

my $dir_name = $ENV{'PWD'};
$dir_name =~ s/\S*\///g;

print "out =\nobj =\ndirs =\n";

my $line;
while($line = <$Build>)
{
	my @dep = ();
	if($line =~ /:/) # Has Dependencies
	{
		my @temp = split(/:/, $line);
		$target = $temp[0];
		@dep[$#dep + 1] = $_ foreach (split(/[,\n]/,$temp[1]));
	}
	else
	{
		$target = $line;
		$target =~ s/\n//;
	}

	foreach(@dep)
	{
		my @_dep = split(/=/);
		print "ifeq (\$(", @_dep[0], "),", @_dep[1], ")", "\n";
	}

	if($target =~ /\//) {print "dirs += ", $target} 
	elsif($target =~ /.elf/){print "out += ", $target}
	else {$target =~ s/\.[Sc]/\.o/; print "obj += ", $target}
	print "\n";
	print "endif\n" foreach(@dep);
}

print 'all: $(obj) $(dirs) $(out)', "\n";
print 'ifneq ($(obj),)', "\n\t", '@echo -e "  LD      " ', "$dir_name.o;\n\t",
	'@if [[ -e link.ld ]]; then $(LD) $(LDFLAGS) -r -T link.ld $(obj) -o ',
	"$dir_name.o; \\\n", "\t", 'else $(LD) $(LDFLAGS) -r $(obj) -o ', "$dir_name.o; ", "fi;\n";
print "endif\n";
print '.PHONY: $(dirs)', "\n";
print '$(dirs): $(patsubst %/,%/Makefile, $(dirs))', "\n";
print "\t", '@echo -e "  MK      " $@', "\n\t", '@$(MAKE) -C $@ $(param)', "\n";
print '$(patsubst %/,%/Makefile, $(dirs)): $(patsubst %/,%/Build, $(dirs))', "\n";
print "\t", '@echo -e "  PL      " Makefile', "\n\t", '@cd $(dir $@) && $(PERL) $(PDIR)/gen.pl > Makefile', "\n";
print "%.o:%.c\n", "\t", '@echo -e "  CC      " $@;', "\n\t", '@ $(CC) $(CFLAGS) -c $< -o $@', "\n";
print "%.o:%.S\n", "\t", '@echo -e "  AS      " $@;', "\n\t", '@ $(AS) $(ASFLAGS) -c $< -o $@', "\n";
print "%.elf:\n", "\t",  '@echo -e "  ELF     " $@;', "\n\t", '@ $(CC) $(CFLAGS) -Wl,-Tlink.ld -lgcc -o $@', "\n";
print '.PHONY: clean', "\n", 'clean: param = clean', "\n", 'clean: $(dirs)', "\n\t",
	'$(RM) $(obj) $(out)', " $dir_name.o\n";

print ".PHONY: distclean\n", "distclean: param = distclean\n", 'distclean: $(dirs) clean', "\n\t",
	'$(RM) Makefile', "\n";
