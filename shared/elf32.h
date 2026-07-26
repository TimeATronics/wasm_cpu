#ifndef S32_ELF32_H
#define S32_ELF32_H

#include <stdint.h>

/* ELF identification indices */
#define EI_MAG0     0
#define EI_MAG1     1
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_OSABI    7
#define EI_ABIVERSION 8
#define EI_PAD      9
#define EI_NIDENT   16

/* Magic bytes */
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

/* Classes */
#define ELFCLASS32  1

/* Data encodings */
#define ELFDATA2LSB 1

/* Version */
#define EV_CURRENT  1

/* OS/ABI */
#define ELFOSABI_NONE   0

/* ELF types */
#define ET_NONE     0
#define ET_REL      1
#define ET_EXEC     2

/* Machine - custom for WASM-S32 */
#define EM_WASM32   0x9999

/* Section types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHT_REL         9

/* Section flags */
#define SHF_WRITE       1
#define SHF_ALLOC       2
#define SHF_EXECINSTR   4

/* Symbol binding */
#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STB_WEAK        2

/* Symbol types */
#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4

/* Symbol visibility */
#define STV_DEFAULT  0
#define STV_HIDDEN   1

/* Special section indices */
#define SHN_UNDEF       0
#define SHN_ABS         0xFFF1
#define SHN_COMMON      0xFFF2

/* WASM-S32 relocation types */
#define R_WASM32_NONE   0
#define R_WASM32_32     1   /* 32-bit absolute address */

/* Program header types */
#define PT_NULL         0
#define PT_LOAD         1

/* Program header flags */
#define PF_X            1
#define PF_W            2
#define PF_R            4

#pragma pack(push, 1)

/* ELF32 header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

/* Section header */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

/* Symbol table entry */
typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

/* Relocation entry with addend */
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} Elf32_Rela;

/* Program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

#pragma pack(pop)

/* Helper macros */
#define ELF32_ST_BIND(i)    ((i) >> 4)
#define ELF32_ST_TYPE(i)    ((i) & 0xf)
#define ELF32_ST_INFO(b,t)  (((b) << 4) | ((t) & 0xf))

#define ELF32_R_SYM(i)      ((i) >> 8)
#define ELF32_R_TYPE(i)     ((i) & 0xff)
#define ELF32_R_INFO(s,t)   (((s) << 8) | ((t) & 0xff))

#endif /* S32_ELF32_H */
