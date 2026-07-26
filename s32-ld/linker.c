#include "linker.h"
#include "../shared/elf32.h"
#include <stdlib.h>
#include <string.h>

/* Object file representation */

typedef struct {
    char         *path;
    FILE         *file;
    Elf32_Ehdr    ehdr;
    Elf32_Shdr   *shdrs;
    int           shnum;
    uint8_t     **sec_data;   /* raw data for each section */

    /* Cached section pointers */
    uint8_t      *text_data;
    int           text_size;
    Elf32_Sym    *syms;
    int           sym_count;
    char         *sym_strtab;
    Elf32_Rela   *relas;
    int           rela_count;

    /* Offset of this object's .text in the merged output */
    size_t        merged_offset;
} ObjectFile;

/* Global symbol table */

typedef struct {
    char     *name;
    uint32_t  value;     /* resolved address in merged output */
    uint16_t  shndx;     /* section index in defining object */
    int       obj_idx;   /* which object defines this symbol (-1 = undef) */
    bool      resolved;
} GlobalSym;

typedef struct {
    GlobalSym *syms;
    int        count;
    int        cap;
} GlobalSymTab;

static void gsym_init(GlobalSymTab *gt) {
    gt->cap = 256;
    gt->count = 0;
    gt->syms = malloc(sizeof(GlobalSym) * gt->cap);
}

static int gsym_lookup(GlobalSymTab *gt, const char *name) {
    for (int i = 0; i < gt->count; i++)
        if (strcmp(gt->syms[i].name, name) == 0)
            return i;
    return -1;
}

static int gsym_add(GlobalSymTab *gt, const char *name, uint32_t value,
                    uint16_t shndx, int obj_idx, bool resolved) {
    if (gt->count >= gt->cap) {
        gt->cap *= 2;
        gt->syms = realloc(gt->syms, sizeof(GlobalSym) * gt->cap);
    }
    int i = gt->count++;
    gt->syms[i].name = strdup(name);
    gt->syms[i].value = value;
    gt->syms[i].shndx = shndx;
    gt->syms[i].obj_idx = obj_idx;
    gt->syms[i].resolved = resolved;
    return i;
}

static void gsym_free(GlobalSymTab *gt) {
    for (int i = 0; i < gt->count; i++)
        free(gt->syms[i].name);
    free(gt->syms);
}

/* Object file reading */

static bool obj_read(ObjectFile *obj, const char *path) {
    memset(obj, 0, sizeof(*obj));
    obj->path = strdup(path);

    obj->file = fopen(path, "rb");
    if (!obj->file) {
        fprintf(stderr, "s32-ld: cannot open '%s'\n", path);
        return false;
    }

    /* Read ELF header */
    if (fread(&obj->ehdr, sizeof(obj->ehdr), 1, obj->file) != 1) {
        fprintf(stderr, "s32-ld: '%s': not a valid ELF file\n", path);
        return false;
    }

    /* Validate ELF magic and machine */
    if (obj->ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        obj->ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        obj->ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        obj->ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "s32-ld: '%s': not an ELF file\n", path);
        return false;
    }
    if (obj->ehdr.e_machine != EM_WASM32) {
        fprintf(stderr, "s32-ld: '%s': not a WASM32 object (machine=0x%x)\n",
                path, obj->ehdr.e_machine);
        return false;
    }
    if (obj->ehdr.e_type != ET_REL) {
        fprintf(stderr, "s32-ld: '%s': not a relocatable object (type=%d)\n",
                path, obj->ehdr.e_type);
        return false;
    }

    /* Read section headers */
    obj->shnum = obj->ehdr.e_shnum;
    obj->shdrs = malloc(sizeof(Elf32_Shdr) * obj->shnum);
    fseek(obj->file, obj->ehdr.e_shoff, SEEK_SET);
    if (fread(obj->shdrs, sizeof(Elf32_Shdr), obj->shnum, obj->file) != (size_t)obj->shnum) {
        fprintf(stderr, "s32-ld: '%s': cannot read section headers\n", path);
        return false;
    }

    /* Read section data */
    obj->sec_data = calloc(sizeof(uint8_t*), obj->shnum);
    for (int i = 0; i < obj->shnum; i++) {
        if (obj->shdrs[i].sh_type != SHT_NULL &&
            obj->shdrs[i].sh_type != SHT_NOBITS &&
            obj->shdrs[i].sh_size > 0) {
            obj->sec_data[i] = malloc(obj->shdrs[i].sh_size);
            fseek(obj->file, obj->shdrs[i].sh_offset, SEEK_SET);
            fread(obj->sec_data[i], 1, obj->shdrs[i].sh_size, obj->file);
        }
    }

    return true;
}

static void obj_cache_sections(ObjectFile *obj) {
    for (int i = 0; i < obj->shnum; i++) {
        if (obj->shdrs[i].sh_type == SHT_PROGBITS &&
            (obj->shdrs[i].sh_flags & SHF_EXECINSTR)) {
            /* .text section */
            obj->text_data = obj->sec_data[i];
            obj->text_size = obj->shdrs[i].sh_size;
        }
        else if (obj->shdrs[i].sh_type == SHT_SYMTAB) {
            obj->syms = (Elf32_Sym*)obj->sec_data[i];
            obj->sym_count = obj->shdrs[i].sh_size / sizeof(Elf32_Sym);
            /* Get associated .strtab */
            int strtab_idx = obj->shdrs[i].sh_link;
            if (strtab_idx > 0 && strtab_idx < obj->shnum)
                obj->sym_strtab = (char*)obj->sec_data[strtab_idx];
        }
        else if (obj->shdrs[i].sh_type == SHT_RELA) {
            obj->relas = (Elf32_Rela*)obj->sec_data[i];
            obj->rela_count = obj->shdrs[i].sh_size / sizeof(Elf32_Rela);
        }
    }
}

static void obj_free(ObjectFile *obj) {
    if (obj->file) fclose(obj->file);
    free(obj->path);
    free(obj->shdrs);
    if (obj->sec_data) {
        for (int i = 0; i < obj->shnum; i++)
            free(obj->sec_data[i]);
        free(obj->sec_data);
    }
}

/* ELF executable output writer */

static void write_elf_exec(FILE *out, uint8_t *merged_text, int merged_size,
                           uint32_t entry, GlobalSymTab *gt,
                           ObjectFile *objs, int obj_count,
                           LinkerConfig *cfg) {
    /* Calculate section layout */
    uint32_t base_vaddr = 0x00000000;
    uint32_t align = 0x1000;

    /* For this first version, write a minimal ELF executable:
     *   - ELF header
     *   - Program header (one PT_LOAD)
     *   - .text section data
     *   - Section headers (.shstrtab, .symtab, .strtab) - optional
     */

    /* Build .shstrtab */
    const char *sec_names[] = {"", ".text", ".shstrtab", ".symtab", ".strtab"};
    int sec_count = sizeof(sec_names) / sizeof(sec_names[0]);

    /* Calculate string table sizes */
    int shstrtab_size = 1;
    for (int i = 0; i < sec_count; i++)
        shstrtab_size += strlen(sec_names[i]) + 1;

    /* Build symbol table from global symtab (only defined symbols) */
    int defined_sym_count = 0;
    for (int i = 0; i < gt->count; i++)
        if (gt->syms[i].resolved) defined_sym_count++;
    int total_syms = 1 + defined_sym_count; /* null + defined */

    /* Build strtab for symbols */
    int strtab_size = 1;
    for (int i = 0; i < gt->count; i++)
        if (gt->syms[i].resolved)
            strtab_size += strlen(gt->syms[i].name) + 1;

    /* Calculate file offsets */
    size_t off = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr);

    size_t text_file_off = off;
    size_t text_file_size = merged_size;
    off += text_file_size;
    while (off % 4 != 0) off++;

    size_t shstrtab_off = off;
    off += shstrtab_size;

    size_t symtab_off = off;
    size_t symtab_size = total_syms * sizeof(Elf32_Sym);
    off += symtab_size;

    size_t strtab_off = off;
    off += strtab_size;

    size_t shoff = off;
    int shnum = sec_count;

    /* Write ELF header */
    rewind(out);
    Elf32_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_WASM32;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = entry;
    ehdr.e_phoff = sizeof(Elf32_Ehdr);
    ehdr.e_shoff = shoff;
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum = 1;
    ehdr.e_shentsize = sizeof(Elf32_Shdr);
    ehdr.e_shnum = shnum;
    ehdr.e_shstrndx = 2;
    fwrite(&ehdr, sizeof(ehdr), 1, out);

    /* Write program header */
    Elf32_Phdr phdr;
    memset(&phdr, 0, sizeof(phdr));
    phdr.p_type = PT_LOAD;
    phdr.p_offset = (uint32_t)text_file_off;
    phdr.p_vaddr = base_vaddr;
    phdr.p_paddr = base_vaddr;
    phdr.p_filesz = (uint32_t)text_file_size;
    phdr.p_memsz = (uint32_t)text_file_size;
    phdr.p_flags = PF_R | PF_X;
    phdr.p_align = align;
    fwrite(&phdr, sizeof(phdr), 1, out);

    /* Write .text */
    fseek(out, (long)text_file_off, SEEK_SET);
    fwrite(merged_text, 1, merged_size, out);

    /* Write .shstrtab */
    fseek(out, (long)shstrtab_off, SEEK_SET);
    fputc(0, out);
    for (int i = 0; i < sec_count; i++)
        fwrite(sec_names[i], 1, strlen(sec_names[i]) + 1, out);

    /* Write .symtab */
    fseek(out, (long)symtab_off, SEEK_SET);
    Elf32_Sym null_sym;
    memset(&null_sym, 0, sizeof(null_sym));
    fwrite(&null_sym, sizeof(null_sym), 1, out);

    for (int i = 0; i < gt->count; i++) {
        if (!gt->syms[i].resolved) continue;
        Elf32_Sym sym;
        memset(&sym, 0, sizeof(sym));
        /* Calculate name offset in .strtab */
        int name_off = 1; /* skip null byte */
        for (int j = 0; j < i; j++)
            if (gt->syms[j].resolved)
                name_off += strlen(gt->syms[j].name) + 1;
        sym.st_name = name_off;
        sym.st_value = gt->syms[i].value;
        sym.st_info = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
        sym.st_shndx = 1; /* .text section */
        fwrite(&sym, sizeof(sym), 1, out);
    }

    /* Write .strtab */
    fseek(out, (long)strtab_off, SEEK_SET);
    fputc(0, out);
    for (int i = 0; i < gt->count; i++)
        if (gt->syms[i].resolved)
            fwrite(gt->syms[i].name, 1, strlen(gt->syms[i].name) + 1, out);

    /* Write section headers */
    fseek(out, (long)shoff, SEEK_SET);

    /* Section 0: NULL */
    Elf32_Shdr shdr;
    memset(&shdr, 0, sizeof(shdr));
    fwrite(&shdr, sizeof(shdr), 1, out);

    /* Section 1: .text */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = 1; /* offset of ".text" in .shstrtab */
    shdr.sh_type = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr.sh_addr = base_vaddr;
    shdr.sh_offset = (uint32_t)text_file_off;
    shdr.sh_size = (uint32_t)text_file_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, out);

    /* Section 2: .shstrtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = 8; /* offset of ".shstrtab" in .shstrtab */
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = (uint32_t)shstrtab_off;
    shdr.sh_size = (uint32_t)shstrtab_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, out);

    /* Section 3: .symtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = 18; /* offset of ".symtab" */
    shdr.sh_type = SHT_SYMTAB;
    shdr.sh_offset = (uint32_t)symtab_off;
    shdr.sh_size = (uint32_t)symtab_size;
    shdr.sh_link = 4; /* .strtab index */
    shdr.sh_info = 1; /* first non-local symbol */
    shdr.sh_addralign = 4;
    shdr.sh_entsize = sizeof(Elf32_Sym);
    fwrite(&shdr, sizeof(shdr), 1, out);

    /* Section 4: .strtab */
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = 26; /* offset of ".strtab" */
    shdr.sh_type = SHT_STRTAB;
    shdr.sh_offset = (uint32_t)strtab_off;
    shdr.sh_size = (uint32_t)strtab_size;
    shdr.sh_addralign = 1;
    fwrite(&shdr, sizeof(shdr), 1, out);
}

/* Linker main entry point */

bool linker_link(LinkerConfig *cfg) {
    if (cfg->obj_count == 0) {
        fprintf(stderr, "s32-ld: no input files\n");
        return false;
    }

    /* Read all object files */
    ObjectFile *objs = calloc(sizeof(ObjectFile), cfg->obj_count);

    for (int i = 0; i < cfg->obj_count; i++) {
        if (!obj_read(&objs[i], cfg->obj_paths[i])) {
            for (int j = 0; j < i; j++) obj_free(&objs[j]);
            free(objs);
            return false;
        }
        obj_cache_sections(&objs[i]);
    }

    /* Calculate total merged .text size and per-object offsets */
    size_t total_text_size = 0;
    for (int i = 0; i < cfg->obj_count; i++) {
        objs[i].merged_offset = total_text_size;
        total_text_size += objs[i].text_size;
    }

    /* Allocate merged code buffer */
    uint8_t *merged = calloc(total_text_size, 1);

    /* Build global symbol table */
    GlobalSymTab gt;
    gsym_init(&gt);

    /* First pass: collect all defined global symbols */
    for (int oi = 0; oi < cfg->obj_count; oi++) {
        if (!objs[oi].syms) continue;
        for (int si = 0; si < objs[oi].sym_count; si++) {
            Elf32_Sym *sym = &objs[oi].syms[si];
            if (ELF32_ST_BIND(sym->st_info) == STB_GLOBAL &&
                sym->st_shndx != SHN_UNDEF) {
                const char *name = objs[oi].sym_strtab ?
                    objs[oi].sym_strtab + sym->st_name : "?";
                uint32_t value = sym->st_value + (uint32_t)objs[oi].merged_offset;
                if (gsym_lookup(&gt, name) >= 0) {
                    fprintf(stderr, "s32-ld: duplicate symbol '%s'\n", name);
                    goto fail;
                }
                gsym_add(&gt, name, value, sym->st_shndx, oi, true);
            }
        }
    }

    /* In case main is not found as a global, look for _start or main */
    if (cfg->entry_sym) {
        if (gsym_lookup(&gt, cfg->entry_sym) < 0) {
            /* Entry symbol not found yet - maybe it's in an object file */
        }
    }

    /* Also collect globally-defined symbols that may be local */
    /* This handles the case where a function is defined but not marked .globl */
    /* In our compiler, all function symbols are STB_GLOBAL */
    if (gsym_lookup(&gt, "_start") < 0) {
        /* No _start - that's OK for .o files, the linker will handle it */
    }

    /* Copy .text from each object into merged buffer */
    for (int i = 0; i < cfg->obj_count; i++) {
        if (objs[i].text_data && objs[i].text_size > 0) {
            memcpy(merged + objs[i].merged_offset,
                   objs[i].text_data, objs[i].text_size);
        }
    }

    /* Apply relocations */
    for (int oi = 0; oi < cfg->obj_count; oi++) {
        if (!objs[oi].relas) continue;
        for (int ri = 0; ri < objs[oi].rela_count; ri++) {
            Elf32_Rela *rela = &objs[oi].relas[ri];
            uint32_t sym_idx = ELF32_R_SYM(rela->r_info);
            uint32_t rtype = ELF32_R_TYPE(rela->r_info);

            if (rtype != R_WASM32_32) {
                fprintf(stderr, "s32-ld: unsupported relocation type %d\n", rtype);
                continue;
            }

            /* Look up symbol name */
            const char *sym_name = NULL;
            uint32_t sym_value = 0;
            bool found = false;

            if (sym_idx < (uint32_t)objs[oi].sym_count) {
                sym_name = objs[oi].sym_strtab ?
                    objs[oi].sym_strtab + objs[oi].syms[sym_idx].st_name : "?";
            }

            /* Check if it's a local symbol (defined in this object, not exported) */
            if (sym_idx < (uint32_t)objs[oi].sym_count) {
                Elf32_Sym *sym = &objs[oi].syms[sym_idx];
                if (sym->st_shndx != SHN_UNDEF) {
                    /* Local symbol - defined in this object */
                    sym_value = sym->st_value + (uint32_t)objs[oi].merged_offset;
                    found = true;
                }
            }

            if (!found && sym_name) {
                /* Look up in global symbol table */
                int gidx = gsym_lookup(&gt, sym_name);
                if (gidx >= 0 && gt.syms[gidx].resolved) {
                    sym_value = gt.syms[gidx].value;
                    found = true;
                }
            }

            if (!found) {
                fprintf(stderr, "s32-ld: undefined reference to '%s' in '%s'\n",
                        sym_name ? sym_name : "?", objs[oi].path);
                /* Continue - patch with 0 */
                sym_value = 0;
            }

            /* Patch at the relocation offset */
            uint32_t patch_addr = (uint32_t)(objs[oi].merged_offset + rela->r_offset);
            uint32_t resolved = sym_value + rela->r_addend;

            if (patch_addr + 4 <= total_text_size) {
                merged[patch_addr]     = resolved & 0xFF;
                merged[patch_addr + 1] = (resolved >> 8) & 0xFF;
                merged[patch_addr + 2] = (resolved >> 16) & 0xFF;
                merged[patch_addr + 3] = (resolved >> 24) & 0xFF;
            }
        }
    }

    /* Patch _start's frame_size to total binary size if _start exists */
    {
        int start_idx = gsym_lookup(&gt, "_start");
        if (start_idx >= 0 && gt.syms[start_idx].resolved) {
            uint32_t start_addr = gt.syms[start_idx].value;
            /* _start is: push <placeholder>; >r; call main; halt
             * push at +0 is 5 bytes, operand at +1 through +4 */
            if (start_addr + 5 <= total_text_size) {
                uint32_t total = (uint32_t)total_text_size;
                merged[start_addr + 1] = total & 0xFF;
                merged[start_addr + 2] = (total >> 8) & 0xFF;
                merged[start_addr + 3] = (total >> 16) & 0xFF;
                merged[start_addr + 4] = (total >> 24) & 0xFF;
            }
        }
    }

    /* Write output */
    FILE *out = fopen(cfg->output_path, "wb");
    if (!out) {
        fprintf(stderr, "s32-ld: cannot open '%s'\n", cfg->output_path);
        goto fail;
    }

    /* Determine entry point: _start must be provided by crt0.o or user code */
    uint32_t entry = 0;
    if (cfg->entry_sym) {
        int gidx = gsym_lookup(&gt, cfg->entry_sym);
        if (gidx >= 0 && gt.syms[gidx].resolved)
            entry = gt.syms[gidx].value;
        else
            fprintf(stderr, "s32-ld: warning: entry point '%s' not found\n", cfg->entry_sym);
    }

    /* Write as ELF executable with program headers */
    write_elf_exec(out, merged, (int)total_text_size, entry, &gt,
                   objs, cfg->obj_count, cfg);
    fclose(out);

    /* Also write flat binary for the simulator */
    char flat_path[1024];
    snprintf(flat_path, sizeof(flat_path), "%s.bin", cfg->output_path);
    FILE *flat = fopen(flat_path, "wb");
    if (flat) {
        fwrite(merged, 1, total_text_size, flat);
        fclose(flat);
    }

    free(merged);
    gsym_free(&gt);
    for (int i = 0; i < cfg->obj_count; i++)
        obj_free(&objs[i]);
    free(objs);
    return true;

fail:
    free(merged);
    gsym_free(&gt);
    for (int i = 0; i < cfg->obj_count; i++)
        obj_free(&objs[i]);
    free(objs);
    return false;
}
