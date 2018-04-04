.code16

.global trampoline
trampoline:
	movw $0x1400, %ax
	movw %ax, %ds
	lgdt %ds:(0x00)
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0
	ljmp $0x8,$pmode_trampoline - trampoline

.code32


.extern gdt_pointer
.extern x86_ap_init
.global pmode_trampoline
pmode_trampoline:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %ss
	movw %ax, %es
	incl (ap_done)
	movl $0x8000, %esp
	lgdtl (gdt_pointer)
	//ljmp $0x8,$x86_ap_init

.global trampoline_end
trampoline_end:

.global ap_done
ap_done: .long 0
