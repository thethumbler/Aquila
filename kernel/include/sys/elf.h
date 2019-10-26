#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

typedef int32_t  elf32_sword;
typedef uint32_t elf32_word;
typedef uint32_t elf32_addr;
typedef uint32_t elf32_off;
typedef uint16_t elf32_half;

#define ET_NONE     0x0000
#define ET_REL      0x0001
#define ET_EXEC     0x0002
#define ET_DYN      0x0003
#define ET_CORE     0x0004
#define ET_LOPROC   0xff00
#define ET_HIPROC   0xffff

#define EM_NONE     0x0000
#define EM_M32      0x0001
#define EM_SPARC    0x0002
#define EM_386      0x0003
#define EM_68K      0x0004
#define EM_88K      0x0005
#define EM_860      0x0007
#define EM_MIPS     0x0008

#define EV_NONE     0x0000
#define EV_CURRENT  0x0001

#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_PAD      7
#define EI_NIDENT   16

#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2

#define SHN_UNDEF       0x0000
#define SHN_LORESERVE   0xff00
#define SHN_LOPROC      0xff00
#define SHN_HIPROC      0xff1f
#define SHN_ABS         0xfff1
#define SHN_COMMON      0xfff2
#define SHN_HIRESERVE   0xffff

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_SHLIB       10
#define SHT_DYNSYM      11
#define SHT_LOPROC      0x70000000
#define SHT_HIPROC      0x7fffffff
#define SHT_LOUSER      0x80000000
#define SHT_HIUSER      0xffffffff

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_SYMBOLIC 16
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_PLTREL   20
#define DT_DEBUG    21
#define DT_TEXTREL  22
#define DT_JMPREL   23
#define DT_LOPROC   0x70000000
#define DT_HIPROC   0x7fffffff

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6
#define PT_LOPROC   0x70000000
#define PT_HIPROC   0x7fffffff

#define PF_X        0x1
#define PF_W        0x2
#define PF_R        0x4
#define PF_MASKPROC 0xf0000000

#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define ELF32_ST_INFO(b,t) (((b) << 4) + ((t) & 0xf))

#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define STB_LOPROC  13
#define STB_HIPROC  15

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_LOPROC  13
#define STT_HIPROC  15

/**
 * \ingroup binfmt
 * \brief elf32 file header
 */
struct elf32_hdr {
    uint8_t     e_ident[EI_NIDENT];
    elf32_half  e_type;
    elf32_half  e_machine;
    elf32_word  e_version;
    elf32_addr  e_entry;
    elf32_off   e_phoff;
    elf32_off   e_shoff;
    elf32_word  e_flags;
    elf32_half  e_ehsize;
    elf32_half  e_phentsize;
    elf32_half  e_phnum;
    elf32_half  e_shentsize;
    elf32_half  e_shnum;
    elf32_half  e_shstrndx;
};

/**
 * \ingroup binfmt
 * \brief elf32 section header
 */
struct elf32_shdr {
    elf32_word  sh_name;
    elf32_word  sh_type;
    elf32_word  sh_flags;
    elf32_addr  sh_addr;
    elf32_off   sh_offset;
    elf32_word  sh_size;
    elf32_word  sh_link;
    elf32_word  sh_info;
    elf32_word  sh_addralign;
    elf32_word  sh_entsize;
};

/**
 * \ingroup binfmt
 * \brief elf32 symbol
 */
struct elf32_sym {
    elf32_word  st_name;
    elf32_word  st_value;
    elf32_word  st_size;
    uint8_t     st_info;
    uint8_t     st_other;
    elf32_half  st_shndx;
};

/**
 * \ingroup binfmt
 * \brief elf32 program header
 */
struct elf32_phdr {
    elf32_word  p_type;
    elf32_off   p_offset;
    elf32_addr  p_vaddr;
    elf32_addr  p_paddr;
    elf32_word  p_filesz;
    elf32_word  p_memsz;
    elf32_word  p_flags;
    elf32_word  p_align;
};

/**
 * \ingroup binfmt
 * \brief elf32 dynamic entry
 */
struct elf32_dyn {
    elf32_sword     d_tag;
    union {
        elf32_word  d_val;
        elf32_addr  d_ptr;
    } d_un;
};

#endif /* ! _ELF_H */
