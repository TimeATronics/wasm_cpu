#ifndef S32AS_ASSEMBLER_H
#define S32AS_ASSEMBLER_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *data;
    int count;
    int cap;
} ByteBuffer;

typedef struct {
    char *name;
    int addr;
    bool is_global;
} Label;

typedef struct {
    char *name;
    int offset;
} Fixup;

typedef struct {
    ByteBuffer code;
    Label *labels;
    int label_count;
    int label_cap;
    Fixup *fixups;
    int fixup_count;
    int fixup_cap;
    char *input_path;
    bool has_error;
    char error_buf[256];
} Assembler;

void assembler_init(Assembler *as, const char *input_path);
bool assembler_assemble(Assembler *as, FILE *in);
bool assembler_write_elf_obj(Assembler *as, FILE *out);
bool assembler_write_flat(Assembler *as, FILE *out);
void assembler_free(Assembler *as);

#endif
