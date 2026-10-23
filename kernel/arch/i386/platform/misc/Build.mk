obj-y                          += irq.o
obj-$(PLATFORM_X86_MISC_PIC)   += pic.o
obj-$(PLATFORM_X86_MISC_APIC)  += apic.o
obj-$(PLATFORM_X86_MISC_PIT)   += pit.o
obj-$(PLATFORM_X86_MISC_CMOS)  += cmos.o
obj-$(PLATFORM_X86_MISC_HPET)  += hpet.o
obj-$(PLATFORM_X86_MISC_ACPI)  += acpi.o
