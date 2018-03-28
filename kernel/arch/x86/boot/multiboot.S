/**********************************************************************
 *						Multiboot support
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <config.h>
#define ASM_FILE
#include <boot/multiboot.h>

/*
 * 	Multiboot definitions
 */

#if MULTIBOOT_GFX
#define MULTIBOOT_GFX_ENABLE    MULTIBOOT_VIDEO_MODE
#else
#define MULTIBOOT_GFX_ENABLE    0
#endif

#define MULTIBOOT_FLAGS  MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_GFX_ENABLE
#define MULTIBOOT_CHKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)

/*
 *	Multiboot lookup header
 */

/* struct multiboot_header */
.align 8
.long MULTIBOOT_HEADER_MAGIC    /* magic */
.long MULTIBOOT_FLAGS           /* flags */
.long MULTIBOOT_CHKSUM          /* checksum */
.long 0                         /* header_addr */
.long 0                         /* heload_addr */
.long 0                         /* heload_end_addr */
.long 0                         /* hebss_end_addr */
.long 0                         /* heentry_addr */

.long 0                         /* mode_type */
.long 1024                       /* mowidth */
.long 768                       /* moheight */
.long 24                        /* modepth */

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
