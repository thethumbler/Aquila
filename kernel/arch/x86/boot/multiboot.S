/**********************************************************************
 *						Multiboot support
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#define ASM_FILE
#include <boot/multiboot.h>

/*
 * 	Multiboot definitions
 */

#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
#define MULTIBOOT_CHKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)

/*
 *	Multiboot lookup header
 */

.align 8
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHKSUM

/*
 * Entry point of the bootloader
 */

.global _start
_start:
	movl %eax, (multiboot_signature)
	movl %ebx, (multiboot_info)

    and  $-16, %esp /* Align stack to 16 byte */

    cli

	.extern early_init
	call early_init
	jmp .

.global multiboot_signature
.global multiboot_info

multiboot_signature: .long 0
multiboot_info: .long 0
