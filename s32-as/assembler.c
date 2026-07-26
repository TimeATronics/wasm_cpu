#include "assembler.h"
#include "../shared/elf32.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ByteBuffer helpers */

static void bb_init(ByteBuffer *bb) {
    bb->cap = 4096;
    bb->data = malloc(bb->cap);
    bb->count = 0;
}

static void bb_write(ByteBuffer *bb, uint8_t byte) {
    if (bb->count >= bb->cap) {
        bb->cap *= 2;
        bb->data = realloc(bb->data, bb->cap);
    }
    bb->data[bb->count++] = byte;
}

static void bb_write_u32(ByteBuffer *bb, uint32_t val) {
    bb_write(bb, val & 0xFF);
    bb_write(bb, (val >> 8) & 0xFF);
    bb_write(bb, (val >> 16) & 0xFF);
    bb_write(bb, (val >> 24) & 0xFF);
}

static void bb_concat(ByteBuffer *bb, uint8_t *data, int len) {
    for (int i = 0; i < len; i++)
        bb_write(bb, data[i]);
}

static void bb_free(ByteBuffer *bb) {
    free(bb->data);
}

/* Helper functions */

static int parse_int(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int)strtol(s, NULL, 16);
    return atoi(s);
}

/* Opcode table */

typedef struct {
    const char *name;
    uint8_t opcode;
    int operand_size; /* 0=no operand, 1=1-byte idx, 4=4-byte addr */
    bool is_branch;   /* call/jump/br_if - may reference labels */
} OpcodeEntry;

#define OP_ENTRY(name, op, opsize, branch) {name, op, opsize, branch}

static const OpcodeEntry opcode_table[] = {
    OP_ENTRY("push",      0x01, 4, false),
    OP_ENTRY("add",       0x02, 0, false),
    OP_ENTRY("sub",       0x03, 0, false),
    OP_ENTRY("mul",       0x04, 0, false),
    OP_ENTRY("drop",      0x05, 0, false),
    OP_ENTRY("print",     0x08, 0, false),
    OP_ENTRY("eq",        0x09, 0, false),
    OP_ENTRY("lt_s",      0x0A, 0, false),
    OP_ENTRY("gt_s",      0x0B, 0, false),
    OP_ENTRY("lt_u",      0x0C, 0, false),
    OP_ENTRY("gt_u",      0x0D, 0, false),
    OP_ENTRY("br_if",     0x0E, 4, true),
    OP_ENTRY("jump",      0x0F, 4, true),
    OP_ENTRY("call",      0x10, 4, true),
    OP_ENTRY("return",    0x11, 0, false),
    OP_ENTRY("dup",       0x12, 0, false),
    OP_ENTRY("swap",      0x13, 0, false),
    OP_ENTRY("over",      0x14, 0, false),
    OP_ENTRY("rot",       0x15, 0, false),
    OP_ENTRY("and",       0x16, 0, false),
    OP_ENTRY("or",        0x17, 0, false),
    OP_ENTRY("xor",       0x18, 0, false),
    OP_ENTRY("not",       0x19, 0, false),
    OP_ENTRY("shl",       0x1A, 0, false),
    OP_ENTRY("shr_u",     0x1B, 0, false),
    OP_ENTRY("shr_s",     0x1C, 0, false),
    OP_ENTRY("load",      0x1D, 0, false),
    OP_ENTRY("store",     0x1E, 0, false),
    OP_ENTRY("key",       0x1F, 0, false),
    OP_ENTRY(">r",        0x30, 0, false),
    OP_ENTRY("r>",        0x31, 0, false),
    OP_ENTRY("r@",        0x32, 0, false),
    OP_ENTRY("depth",     0x33, 0, false),
    OP_ENTRY("rdepth",    0x34, 0, false),
    OP_ENTRY("eqz",       0x35, 0, false),
    OP_ENTRY("div_s",     0x36, 0, false),
    OP_ENTRY("load8_u",   0x37, 0, false),
    OP_ENTRY("store8",    0x38, 0, false),
    OP_ENTRY("local.get", 0x39, 1, false),
    OP_ENTRY("local.set", 0x3A, 1, false),
    OP_ENTRY("sysenter",  0x3B, 0, false),
    OP_ENTRY("eret",      0x3C, 0, false),
    OP_ENTRY("csr_read",  0x3D, 4, false),
    OP_ENTRY("csr_write", 0x3E, 4, false),
    OP_ENTRY("tlb_flush", 0x3F, 0, false),
    OP_ENTRY("get_fp",    0x40, 0, false),
    OP_ENTRY("fadd",      0x41, 0, false),
    OP_ENTRY("fsub",      0x42, 0, false),
    OP_ENTRY("fmul",      0x43, 0, false),
    OP_ENTRY("fdiv",      0x44, 0, false),
    OP_ENTRY("fcmp",      0x45, 0, false),
    OP_ENTRY("f2i",       0x46, 0, false),
    OP_ENTRY("i2f",       0x47, 0, false),
    OP_ENTRY("halt",      0xFF, 0, false),
};

static const int opcode_count = sizeof(opcode_table) / sizeof(opcode_table[0]);

static const OpcodeEntry *find_opcode(const char *name) {
    for (int i = 0; i < opcode_count; i++)
        if (strcmp(opcode_table[i].name, name) == 0)
            return &opcode_table[i];
    return NULL;
}

/* Helper functions */

static bool is_global_label(Assembler *as, const char *name) {
    for (int i = 0; i < as->label_count; i++)
        if (strcmp(as->labels[i].name, name) == 0)
            return as->labels[i].is_global;
    return false;
}

/* Assembler implementation */

void assembler_init(Assembler *as, const char *input_path) {
    memset(as, 0, sizeof(*as));
    bb_init(&as->code);
    as->label_cap = 256;
    as->labels = malloc(sizeof(Label) * as->label_cap);
    as->fixup_cap = 256;
    as->fixups = malloc(sizeof(Fixup) * as->fixup_cap);
    as->input_path = strdup(input_path);
}

static void as_error(Assembler *as, const char *msg, int line) {
    if (!as->has_error) {
        snprintf(as->error_buf, sizeof(as->error_buf),
                 "%s:%d: %s", as->input_path, line, msg);
        as->has_error = true;
    }
}

static int add_label(Assembler *as, const char *name, int addr, bool is_global) {
    for (int i = 0; i < as->label_count; i++) {
        if (strcmp(as->labels[i].name, name) == 0) {
            if (addr >= 0) as->labels[i].addr = addr;
            as->labels[i].is_global = as->labels[i].is_global || is_global;
            return i;
        }
    }
    if (as->label_count >= as->label_cap) {
        as->label_cap *= 2;
        as->labels = realloc(as->labels, sizeof(Label) * as->label_cap);
    }
    as->labels[as->label_count].name = strdup(name);
    as->labels[as->label_count].addr = addr;
    as->labels[as->label_count].is_global = is_global;
    return as->label_count++;
}

static int find_label(Assembler *as, const char *name) {
    for (int i = 0; i < as->label_count; i++)
        if (strcmp(as->labels[i].name, name) == 0)
            return as->labels[i].addr;
    return -1;
}

static void add_fixup(Assembler *as, const char *name, int offset) {
    if (as->fixup_count >= as->fixup_cap) {
        as->fixup_cap *= 2;
        as->fixups = realloc(as->fixups, sizeof(Fixup) * as->fixup_cap);
    }
    as->fixups[as->fixup_count].name = strdup(name);
    as->fixups[as->fixup_count].offset = offset;
    as->fixup_count++;
}

static void strip_comment(char *line) {
    char *p = line;
    bool in_string = false;
    while (*p) {
        if (*p == '"') in_string = !in_string;
        if (*p == ';' && !in_string) { *p = '\0'; break; }
        p++;
    }
}

bool assembler_assemble(Assembler *as, FILE *in) {
    /* Read all lines */
    char **lines = NULL;
    int line_count = 0;
    int line_cap = 256;
    lines = malloc(sizeof(char*) * line_cap);
    
    char buf[4096];
    while (fgets(buf, sizeof(buf), in)) {
        if (line_count >= line_cap) {
            line_cap *= 2;
            lines = realloc(lines, sizeof(char*) * line_cap);
        }
        lines[line_count] = strdup(buf);
        line_count++;
    }
    
    /* Helper macro: strip trailing whitespace (including \r, \n) */
#define TRIM_TRAILING(str) do { \
    char *p = (str) + strlen(str); \
    while (p > (str) && isspace((unsigned char)p[-1])) *--p = '\0'; \
} while(0)

    /* First pass: collect labels, calculate instruction positions */
    for (int i = 0; i < line_count; i++) {
        char tmp[4096];
        strcpy(tmp, lines[i]);
        strip_comment(tmp);
        TRIM_TRAILING(tmp);
        
        char *s = tmp;
        while (*s && isspace((unsigned char)*s)) s++;
        if (*s == '\0') continue;
        
        /* Check if first token is a label (ends with ':') */
        /* We need to distinguish between label definitions like "main:" and
         * label references in operands like ":main" used in call :main */
        /* A label definition has ':' at the end of the first word (before any space) */
        char *first_space = strpbrk(s, " \t");
        bool is_label = false;
        char *colon = NULL;
        
        if (first_space) {
            /* Check if the token before space ends with ':' */
            if (first_space > s && first_space[-1] == ':') {
                is_label = true;
                colon = first_space - 1;
            }
        } else {
            /* Single token - check if it ends with ':' */
            int len = strlen(s);
            if (len > 0 && s[len - 1] == ':') {
                is_label = true;
                colon = s + len - 1;
            }
            /* Also check for "label::" style */
            if (len > 1 && s[len - 2] == ':' && s[len - 1] == ':') {
                is_label = true;
                colon = s + len - 2;
            }
        }
        
        if (is_label && colon) {
            *colon = '\0';
            add_label(as, s, as->code.count, false);
            continue;
        }
        
        /* Directive */
        if (s[0] == '.') {
            char *space = strchr(s, ' ');
            if (!space) space = strchr(s, '\t');
            if (space) *space = '\0';
            
            if (strcmp(s, ".globl") == 0 || strcmp(s, ".global") == 0) {
                if (space) {
                    char *name = space + 1;
                    while (*name && isspace((unsigned char)*name)) name++;
                    add_label(as, name, -1, true);
                }
            } else if (strcmp(s, ".word") == 0) {
                as->code.count += 4;
            } else if (strcmp(s, ".byte") == 0) {
                as->code.count += 1;
            } else if (strcmp(s, ".space") == 0) {
                if (space) {
                    char *val = space + 1;
                    as->code.count += parse_int(val);
                }
            } else if (strcmp(s, ".text") == 0 || strcmp(s, ".data") == 0 ||
                       strcmp(s, ".section") == 0 || strcmp(s, ".align") == 0 ||
                       strcmp(s, ".type") == 0 || strcmp(s, ".size") == 0) {
                /* Ignore section/alignment directives for position calc */
            } else {
                char err[256];
                snprintf(err, sizeof(err), "unknown directive '%s'", s);
                as_error(as, err, i + 1);
                goto cleanup;
            }
            continue;
        }
        
        /* Instruction */
        const OpcodeEntry *op = find_opcode(s);
        if (op) {
            /* Calculate instruction size */
            as->code.count += 1; /* opcode */
            if (op->operand_size > 0)
                as->code.count += op->operand_size;
        } else {
            /* Try splitting on space to get opcode name */
            /* This is needed because s might be "push" but the operand follows */
            /* Actually find_opcode checks the whole string, so if there's a space, it won't match */
            continue;
        }
    }
    
    /* Reset code position for second pass */
    as->code.count = 0;
    
    /* Second pass: emit bytes */
    for (int i = 0; i < line_count; i++) {
        char tmp[4096];
        strcpy(tmp, lines[i]);
        strip_comment(tmp);
        TRIM_TRAILING(tmp);
        
        char *s = tmp;
        while (*s && isspace((unsigned char)*s)) s++;
        if (*s == '\0') continue;
        
        /* Check if first token is a label (ends with ':') */
        {
            char *sp = strchr(s, ' ');
            if (!sp) sp = strchr(s, '\t');
            bool is_label_second = false;
            char *cp = NULL;
            
            if (sp) {
                if (sp > s && sp[-1] == ':') {
                    is_label_second = true;
                    cp = sp - 1;
                }
            } else {
                int ln = strlen(s);
                if (ln > 0 && s[ln - 1] == ':') {
                    is_label_second = true;
                    cp = s + ln - 1;
                }
                if (ln > 1 && s[ln - 2] == ':' && s[ln - 1] == ':') {
                    is_label_second = true;
                    cp = s + ln - 2;
                }
            }
            
            if (is_label_second && cp) {
                /* Label prefix like "main: push 42" or just "main:" */
                char *rest = cp + 1;
                while (*rest && isspace((unsigned char)*rest)) rest++;
                *cp = '\0';
                add_label(as, s, as->code.count, false);
                s = rest;
                if (*s == '\0') continue;
            }
        }
        
        /* Directive */
        if (s[0] == '.') {
            char *space = strchr(s, ' ');
            if (!space) space = strchr(s, '\t');
            
            if (strcmp(s, ".globl") == 0 || strcmp(s, ".global") == 0) {
                /* Already handled in first pass */
            } else if (strcmp(s, ".word") == 0) {
                if (space) {
                    char *val = space + 1;
                    while (*val && isspace((unsigned char)*val)) val++;
                    /* Check if it's a label reference */
                    if (val[0] == ':' || val[0] == '.') {
                        char *label_name = val;
                        if (label_name[0] == ':') label_name++;
                        int addr = find_label(as, label_name);
                        if (addr >= 0) {
                            bb_write_u32(&as->code, addr);
                        } else {
                            /* Forward reference - create fixup */
                            bb_write_u32(&as->code, 0);
                            add_fixup(as, label_name, as->code.count - 4);
                        }
                    } else {
                        bb_write_u32(&as->code, (uint32_t)parse_int(val));
                    }
                }
            } else if (strcmp(s, ".byte") == 0) {
                if (space) {
                    bb_write(&as->code, (uint8_t)parse_int(space + 1));
                }
            } else if (strcmp(s, ".space") == 0) {
                if (space) {
                    int n = parse_int(space + 1);
                    for (int j = 0; j < n; j++)
                        bb_write(&as->code, 0);
                }
            } else if (strcmp(s, ".text") == 0 || strcmp(s, ".data") == 0 ||
                       strcmp(s, ".section") == 0 || strcmp(s, ".align") == 0 ||
                       strcmp(s, ".type") == 0 || strcmp(s, ".size") == 0) {
                /* Ignored */
            }
            continue;
        }
        
        /* Parse opcode + operand */
        char opcode_name[64];
        char operand[256];
        operand[0] = '\0';
        
        char *space = strchr(s, ' ');
        if (!space) space = strchr(s, '\t');
        if (space) {
            int len = (int)(space - s);
            if (len >= 64) len = 63;
            strncpy(opcode_name, s, len);
            opcode_name[len] = '\0';
            char *op_p = space + 1;
            while (*op_p && isspace((unsigned char)*op_p)) op_p++;
            strncpy(operand, op_p, sizeof(operand) - 1);
            operand[sizeof(operand) - 1] = '\0';
        } else {
            strncpy(opcode_name, s, sizeof(opcode_name) - 1);
            opcode_name[sizeof(opcode_name) - 1] = '\0';
        }
        
        const OpcodeEntry *op = find_opcode(opcode_name);
        if (!op) {
            char err[256];
            snprintf(err, sizeof(err), "unknown opcode '%s'", opcode_name);
            as_error(as, err, i + 1);
            goto cleanup;
        }
        
        bb_write(&as->code, op->opcode);
        
        if (op->operand_size == 1) {
            /* local.get, local.set - 1-byte index */
            int val = parse_int(operand);
            bb_write(&as->code, (uint8_t)val);
        } else if (op->operand_size == 4 && op->is_branch) {
            /* call, jump, br_if - 4-byte address or label */
            if (operand[0] == ':' || operand[0] == '.') {
                char *label_name = operand;
                if (label_name[0] == ':') label_name++;
                int addr = find_label(as, label_name);
                if (addr >= 0 && !is_global_label(as, label_name)) {
                    bb_write_u32(&as->code, (uint32_t)addr);
                } else {
                    bb_write_u32(&as->code, 0);
                    add_fixup(as, label_name, as->code.count - 4);
                }
            } else {
                bb_write_u32(&as->code, (uint32_t)parse_int(operand));
            }
        } else if (op->operand_size == 4) {
            /* push, csr_read, csr_write - may reference labels */
            if (isalpha(operand[0]) || operand[0] == '_' || operand[0] == '.') {
                char *label_name = operand;
                if (label_name[0] == ':') label_name++;
                int addr = find_label(as, label_name);
                if (addr >= 0 && !is_global_label(as, label_name)) {
                    bb_write_u32(&as->code, (uint32_t)addr);
                } else {
                    bb_write_u32(&as->code, 0);
                    add_fixup(as, label_name, as->code.count - 4);
                }
            } else {
                bb_write_u32(&as->code, (uint32_t)parse_int(operand));
            }
        }
    }
    
    /* Resolve fixups for non-global labels (global labels keep relocations for the linker) */
    for (int i = 0; i < as->fixup_count; i++) {
        int addr = find_label(as, as->fixups[i].name);
        if (addr >= 0 && !is_global_label(as, as->fixups[i].name)) {
            uint32_t val = (uint32_t)addr;
            as->code.data[as->fixups[i].offset]     = val & 0xFF;
            as->code.data[as->fixups[i].offset + 1] = (val >> 8) & 0xFF;
            as->code.data[as->fixups[i].offset + 2] = (val >> 16) & 0xFF;
            as->code.data[as->fixups[i].offset + 3] = (val >> 24) & 0xFF;
        }
    }
    
    int ret = !as->has_error;
    
cleanup:
    for (int i = 0; i < line_count; i++)
        free(lines[i]);
    free(lines);
    return ret;
}

/* ELF object file output */

static void write_ehdr(FILE *out, uint16_t type, uint16_t shnum, uint32_t shoff, uint16_t shstrndx) {
    Elf32_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = type;
    ehdr.e_machine = EM_WASM32;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = shoff;
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_shentsize = sizeof(Elf32_Shdr);
    ehdr.e_shnum = shnum;
    ehdr.e_shstrndx = shstrndx;
    fwrite(&ehdr, sizeof(ehdr), 1, out);
}

static void write_shdr(FILE *out, uint32_t name, uint32_t type, uint32_t flags,
                       uint32_t offset, uint32_t size, uint32_t link, uint32_t info,
                       uint32_t addralign, uint32_t entsize) {
    Elf32_Shdr shdr;
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = name;
    shdr.sh_type = type;
    shdr.sh_flags = flags;
    shdr.sh_offset = offset;
    shdr.sh_size = size;
    shdr.sh_link = link;
    shdr.sh_info = info;
    shdr.sh_addralign = addralign;
    shdr.sh_entsize = entsize;
    fwrite(&shdr, sizeof(shdr), 1, out);
}

static void write_sym(FILE *out, uint32_t name, uint32_t value, uint32_t size,
                      uint8_t info, uint16_t shndx) {
    Elf32_Sym sym;
    memset(&sym, 0, sizeof(sym));
    sym.st_name = name;
    sym.st_value = value;
    sym.st_size = size;
    sym.st_info = info;
    sym.st_other = STV_DEFAULT;
    sym.st_shndx = shndx;
    fwrite(&sym, sizeof(sym), 1, out);
}

static void write_rela(FILE *out, uint32_t offset, uint32_t info, int32_t addend) {
    Elf32_Rela rela;
    memset(&rela, 0, sizeof(rela));
    rela.r_offset = offset;
    rela.r_info = info;
    rela.r_addend = addend;
    fwrite(&rela, sizeof(rela), 1, out);
}

/* Build string table from an array of strings. Returns the table. */
static int build_strtab(const char **strs, int count, uint8_t **out_data) {
    /* Calculate size: each string is null-terminated. First byte is null. */
    int size = 1;
    for (int i = 0; i < count; i++)
        size += strlen(strs[i]) + 1;
    
    *out_data = malloc(size);
    int pos = 0;
    (*out_data)[pos++] = '\0'; /* index 0 = empty string */
    for (int i = 0; i < count; i++) {
        int len = strlen(strs[i]);
        memcpy(*out_data + pos, strs[i], len + 1);
        pos += len + 1;
    }
    return size;
}

/* Get offset of string in a string table (returns index of first occurrence) */
static int strtab_offset(uint8_t *tab, int tab_size, const char *s) {
    int pos = 0;
    while (pos < tab_size) {
        if (strcmp((char*)(tab + pos), s) == 0)
            return pos;
        pos += strlen((char*)(tab + pos)) + 1;
    }
    return -1;
}

bool assembler_write_elf_obj(Assembler *as, FILE *out) {
    /* Build section name string table (.shstrtab) */
    const char *sec_names[] = {
        "",            /* index 0 = empty */
        ".text",
        ".symtab",
        ".strtab",
        ".shstrtab",
        ".rela.text",
    };
    int sec_count = sizeof(sec_names) / sizeof(sec_names[0]);
    
    uint8_t *shstrtab_data = NULL;
    int shstrtab_size = build_strtab(sec_names, sec_count, &shstrtab_data);
    
    /* Collect unique symbol names from labels */
    /* First, find which labels should be global (exported): non-".L" labels that are global */
    /* Also, find which fixups reference undefined labels (external references) */
    
    /* Count symbols: null entry (0) + defined exported labels + undefined fixup targets */
    int sym_null = 1;
    int sym_exported = 0;
    int sym_undefined = 0;
    
    /* Collect labels that are global (non-".L" or marked as .globl) */
    int *exported_indices = malloc(sizeof(int) * as->label_count);
    for (int i = 0; i < as->label_count; i++) {
        if (as->labels[i].addr >= 0 && as->labels[i].is_global) {
            exported_indices[sym_exported++] = i;
        }
    }
    
    /* Collect fixups referencing undefined labels */
    int *undef_fixups = malloc(sizeof(int) * as->fixup_count);
    int *undef_sym_ids = malloc(sizeof(int) * as->fixup_count);
    for (int i = 0; i < as->fixup_count; i++) {
        int addr = find_label(as, as->fixups[i].name);
        if (addr < 0) {
            /* Check if already collected */
            bool found = false;
            for (int j = 0; j < sym_undefined; j++) {
                if (strcmp(as->fixups[undef_fixups[j]].name, as->fixups[i].name) == 0) {
                    undef_sym_ids[i] = j;
                    found = true;
                    break;
                }
            }
            if (!found) {
                undef_sym_ids[i] = sym_undefined;
                undef_fixups[sym_undefined++] = i;
            }
        }
    }
    
    /* Build symbol name list and string table */
    int sym_total = sym_null + sym_exported + sym_undefined;
    const char **sym_names = malloc(sizeof(char*) * sym_total);
    sym_names[0] = ""; /* null symbol */
    
    /* Created symbols: exported labels */
    int *created_sym_idx = malloc(sizeof(int) * as->label_count);
    for (int i = 0; i < as->label_count; i++)
        created_sym_idx[i] = -1;
    
    for (int i = 0; i < sym_exported; i++) {
        sym_names[sym_null + i] = as->labels[exported_indices[i]].name;
        created_sym_idx[exported_indices[i]] = sym_null + i;
    }
    
    /* Undefined symbol names */
    for (int i = 0; i < sym_undefined; i++) {
        sym_names[sym_null + sym_exported + i] = as->fixups[undef_fixups[i]].name;
    }
    
    /* Build .strtab */
    uint8_t *strtab_data = NULL;
    int strtab_size = build_strtab(sym_names, sym_total, &strtab_data);
    
    /* Build .symtab */
    int symtab_size = sym_total * sizeof(Elf32_Sym);
    uint8_t *symtab_data = malloc(symtab_size);
    memset(symtab_data, 0, symtab_size);
    
    /* Null symbol (index 0) is already all zeros */
    
    /* Exported symbols */
    for (int i = 0; i < sym_exported; i++) {
        int li = exported_indices[i];
        int sym_idx = sym_null + i;
        Elf32_Sym *sym = (Elf32_Sym*)(symtab_data + sym_idx * sizeof(Elf32_Sym));
        sym->st_name = strtab_offset(strtab_data, strtab_size, as->labels[li].name);
        sym->st_value = (uint32_t)as->labels[li].addr;
        sym->st_info = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
        sym->st_shndx = 1; /* .text section (index 1) */
    }
    
    /* Undefined symbols */
    for (int i = 0; i < sym_undefined; i++) {
        int fi = undef_fixups[i];
        int sym_idx = sym_null + sym_exported + i;
        Elf32_Sym *sym = (Elf32_Sym*)(symtab_data + sym_idx * sizeof(Elf32_Sym));
        sym->st_name = strtab_offset(strtab_data, strtab_size, as->fixups[fi].name);
        sym->st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        sym->st_shndx = SHN_UNDEF;
    }
    
    /* Build .rela.text: only for fixups referencing undefined symbols */
    int rela_count = 0;
    for (int i = 0; i < as->fixup_count; i++) {
        if (find_label(as, as->fixups[i].name) < 0)
            rela_count++;
    }
    
    int rela_size = rela_count * sizeof(Elf32_Rela);
    uint8_t *rela_data = malloc(rela_size > 0 ? rela_size : 1);
    memset(rela_data, 0, rela_size > 0 ? rela_size : 1);
    
    int rela_pos = 0;
    for (int i = 0; i < as->fixup_count; i++) {
        if (find_label(as, as->fixups[i].name) < 0) {
            Elf32_Rela *rela = (Elf32_Rela*)(rela_data + rela_pos * sizeof(Elf32_Rela));
            rela->r_offset = (uint32_t)as->fixups[i].offset;
            /* Find symbol index */
            int sym_idx = sym_null + sym_exported + undef_sym_ids[i];
            rela->r_info = ELF32_R_INFO(sym_idx, R_WASM32_32);
            rela->r_addend = 0;
            rela_pos++;
        }
    }
    
    /* Calculate file layout */
    size_t off = sizeof(Elf32_Ehdr);
    
    /* .text section */
    size_t text_off = off;
    size_t text_size = as->code.count;
    off += text_size;
    
    /* Align to 4 bytes */
    while (off % 4 != 0) off++;
    
    /* .rela.text section */
    size_t rela_off = off;
    size_t rela_file_size = rela_size;
    off += rela_file_size;
    
    /* .symtab section */
    size_t symtab_off = off;
    off += symtab_size;
    
    /* .strtab section */
    size_t strtab_off = off;
    off += strtab_size;
    
    /* .shstrtab section */
    size_t shstrtab_off = off;
    off += shstrtab_size;
    
    /* Section headers start here */
    size_t shoff = off;
    int shnum = 6; /* null, .text, .symtab, .strtab, .shstrtab, .rela.text */
    
    /* Write ELF header */
    rewind(out);
    write_ehdr(out, ET_REL, shnum, (uint32_t)shoff, 4);
    
    /* Write .text */
    fseek(out, (long)text_off, SEEK_SET);
    fwrite(as->code.data, 1, text_size, out);
    
    /* Write .rela.text */
    if (rela_size > 0) {
        fseek(out, (long)rela_off, SEEK_SET);
        fwrite(rela_data, 1, rela_size, out);
    }
    
    /* Write .symtab */
    fseek(out, (long)symtab_off, SEEK_SET);
    fwrite(symtab_data, 1, symtab_size, out);
    
    /* Write .strtab */
    fseek(out, (long)strtab_off, SEEK_SET);
    fwrite(strtab_data, 1, strtab_size, out);
    
    /* Write .shstrtab */
    fseek(out, (long)shstrtab_off, SEEK_SET);
    fwrite(shstrtab_data, 1, shstrtab_size, out);
    
    /* Write section headers */
    fseek(out, (long)shoff, SEEK_SET);
    
    /* Section 0: NULL */
    write_shdr(out, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0);
    
    /* Section 1: .text */
    int text_name_off = strtab_offset(shstrtab_data, shstrtab_size, ".text");
    write_shdr(out, text_name_off, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
               (uint32_t)text_off, (uint32_t)text_size, 0, 0, 1, 0);
    
    /* Section 2: .symtab */
    int symtab_name_off = strtab_offset(shstrtab_data, shstrtab_size, ".symtab");
    write_shdr(out, symtab_name_off, SHT_SYMTAB, 0,
               (uint32_t)symtab_off, (uint32_t)symtab_size,
               3, /* sh_link = .strtab index */
               sym_null + sym_exported, /* sh_info = first non-local symbol */
               4, sizeof(Elf32_Sym));
    
    /* Section 3: .strtab */
    int strtab_name_off = strtab_offset(shstrtab_data, shstrtab_size, ".strtab");
    write_shdr(out, strtab_name_off, SHT_STRTAB, 0,
               (uint32_t)strtab_off, (uint32_t)strtab_size, 0, 0, 1, 0);
    
    /* Section 4: .shstrtab */
    int shstrtab_name_off = strtab_offset(shstrtab_data, shstrtab_size, ".shstrtab");
    write_shdr(out, shstrtab_name_off, SHT_STRTAB, 0,
               (uint32_t)shstrtab_off, (uint32_t)shstrtab_size, 0, 0, 1, 0);
    
    /* Section 5: .rela.text */
    int rela_name_off = strtab_offset(shstrtab_data, shstrtab_size, ".rela.text");
    write_shdr(out, rela_name_off, SHT_RELA, 0,
               (uint32_t)rela_off, (uint32_t)rela_file_size,
               2, /* sh_link = .symtab index */
               1, /* sh_info = .text section index */
               4, sizeof(Elf32_Rela));
    
    free(rela_data);
    free(symtab_data);
    free(strtab_data);
    free(shstrtab_data);
    free(sym_names);
    free(exported_indices);
    free(undef_fixups);
    free(undef_sym_ids);
    free(created_sym_idx);
    
    return !as->has_error;
}

bool assembler_write_flat(Assembler *as, FILE *out) {
    /* Resolve remaining fixups */
    for (int i = 0; i < as->fixup_count; i++) {
        int addr = find_label(as, as->fixups[i].name);
        if (addr >= 0) {
            uint32_t val = (uint32_t)addr;
            as->code.data[as->fixups[i].offset]     = val & 0xFF;
            as->code.data[as->fixups[i].offset + 1] = (val >> 8) & 0xFF;
            as->code.data[as->fixups[i].offset + 2] = (val >> 16) & 0xFF;
            as->code.data[as->fixups[i].offset + 3] = (val >> 24) & 0xFF;
        }
    }
    
    fwrite(as->code.data, 1, as->code.count, out);
    return !as->has_error;
}

void assembler_free(Assembler *as) {
    bb_free(&as->code);
    for (int i = 0; i < as->label_count; i++)
        free(as->labels[i].name);
    free(as->labels);
    for (int i = 0; i < as->fixup_count; i++)
        free(as->fixups[i].name);
    free(as->fixups);
    free(as->input_path);
}
