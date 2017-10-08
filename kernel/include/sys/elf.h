#ifndef _ELF_H
#define _ELF_H

#include <core/system.h>
#include <sys/proc.h>

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define SHT_NULL		0
#define SHT_PROGBITS	1

#define SHF_WRITE	0x1
#define SHF_ALLOC	0x2
#define SHF_EXEC	0x4

typedef struct
{
	uint8_t  magic[4];
	uint8_t  class;
	uint8_t  endian;
	uint8_t  elf_version;
	uint8_t  abi;
	uint8_t  abi_version;
	uint8_t  pad[7];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} elf32_hdr_t;

typedef struct
{
	uint32_t name;
	uint32_t type;
	uint32_t flags;
	uint32_t addr;
	uint32_t off;
	uint32_t size;
	uint32_t link;
	uint32_t info;
	uint32_t addralign;
	uint32_t entsize;
} elf32_section_hdr_t;

typedef struct
{
	uint8_t  magic[4];
	uint8_t  class;
	uint8_t  endian;
	uint8_t  elf_version;
	uint8_t  abi;
	uint8_t  abi_version;
	uint8_t  pad[7];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t phoff;
	uint64_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} elf64_hdr_t;

typedef struct
{
	uint32_t name;
	uint32_t type;
	uint64_t flags;
	uint64_t addr;
	uint64_t off;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t addralign;
	uint64_t entsize;
} elf64_section_hdr_t;

/* sys/elf.c */
proc_t *load_elf(const char *fn);
proc_t *load_elf_proc(proc_t *proc, const char *fn);

#endif
