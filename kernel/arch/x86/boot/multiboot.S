#define ASM_FILE
#include <boot/multiboot.h>


/*
 * Multiboot definitions
 */

#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
#define MULTIBOOT_CHKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)

/*
 * Entry point of the bootloader
 */

.global _start
_start:
	jmp _real_start
	.align 8
	.long MULTIBOOT_HEADER_MAGIC
	.long MULTIBOOT_FLAGS
	.long MULTIBOOT_CHKSUM

_real_start:
	movl %eax, (multiboot_signature)
	movl %ebx, (multiboot_info)

	.extern early_init
	call early_init
	jmp .

.global multiboot_signature
.global multiboot_info

multiboot_signature: .long 0
multiboot_info: .long 0
