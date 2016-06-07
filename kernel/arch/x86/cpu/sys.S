.code32

//
//	x86_check_cpuid
//

//	When CPUID instruction is supported, EFLAGS.ID (bit 21) is modifiable
//	so we check if we can modify it

.global x86_check_cpuid
x86_check_cpuid:
	pushf
	pushf
	xorl $1 << 21, (%esp)
	popf
	pushf
	pop %eax
	xorl (%esp), %eax
	andl $1 << 21, %eax
	popf
	ret


//
//	x86_get_vendor_id
//

x86_vendor_id:	
	.fill 13

.global x86_get_vendor_id
x86_get_vendor_id:
	movl $0x0, %eax
	cpuid
	movl %ebx, (x86_vendor_id + 0x0)
	movl %edx, (x86_vendor_id + 0x4)
	movl %ecx, (x86_vendor_id + 0x8)
	movl x86_vendor_id, %eax
	ret

//
//	x86_get_features
//

x86_features: .long 0

.global x86_get_features
x86_get_features:
	movl $0x1, %eax
	cpuid
	mov %edx, (x86_features)
	mov x86_features, %eax
	ret


//
// ISRs -- Interrupt Service Routines
//

.global int_num, err_num
int_num: .long 0
err_num: .long 0

.macro ISR_NOERR v
.global isr\v
isr\v:
	cli
	movl $0x0, (err_num)
	movl $\v, (int_num)
	jmp isr_handler
.endm

.macro ISR_ERR v
.global isr\v
isr\v:
	cli
	popl (err_num)
	movl $\v, (int_num)
	jmp isr_handler
.endm

.macro push_context
	push %eax
	push %edx
	push %ecx
	push %ebx
	push %ebp
	push %esi
	push %edi
.endm
	
.macro pop_context
	pop %edi
	pop %esi
	pop %ebp
	pop %ebx
	pop %ecx
	pop %edx
	pop %eax
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
ISR_NOERR 128


.extern interrupt
.global isr_handler
isr_handler:
	push_context
	push %esp
	call interrupt
	pop %eax
	pop_context
	iret


//
// IRQs -- external interrupt requists (from PIC)
//

.macro IRQ n, i
.global irq\n
irq\n:
	cli
	movl %eax, (err_num)
	movl $\i, (int_num)
	jmp irq_stub
.endm

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47


.extern irq_handler
irq_stub:
	push_context
	push %esp
	call irq_handler
	pop %eax
	pop_context
	iret

.global dum_var
dum_var: .byte 1
.global x86_jump_userspace
x86_jump_userspace:
	pop  %eax 	/* Caller return address */
	mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pop_context
    iret

.global x86_jump_user
x86_jump_user:	/* eax, eip, cs, eflags, esp, ss */
	pop  %eax 	/* Caller return address */
	mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pop	 %eax 	/* eax for sys_fork return */
    iret

.global x86_read_eip
x86_read_eip:
	mov (%esp), %eax
	ret

.global x86_goto
x86_goto:
	pop %ebx	/* Caller return address */
	pop %ebx
	pop %ebp
	pop %esp
	mov $-1, %eax /* Return -1 -> Done switching */
	jmp *%ebx


.extern internal_arch_sleep
.global x86_sleep
x86_sleep:
	push_context
	call internal_arch_sleep
	pop_context
	ret