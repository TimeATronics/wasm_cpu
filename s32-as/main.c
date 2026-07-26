#include "assembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-o output] [-f format] input.s\n", prog);
    fprintf(stderr, "  -o output   Output file (default: a.out)\n");
    fprintf(stderr, "  -f format   Output format: elf (default), flat\n");
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = "a.out";
    bool elf_format = true;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output = argv[++i];
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "flat") == 0)
                elf_format = false;
        } else if (argv[i][0] != '-')
            input = argv[i];
        else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!input) {
        usage(argv[0]);
        return 1;
    }

    FILE *in = fopen(input, "r");
    if (!in) {
        fprintf(stderr, "s32-as: cannot open '%s'\n", input);
        return 1;
    }

    Assembler as;
    assembler_init(&as, input);

    if (!assembler_assemble(&as, in)) {
        fprintf(stderr, "s32-as: %s\n", as.error_buf);
        assembler_free(&as);
        fclose(in);
        return 1;
    }
    fclose(in);

    FILE *out = fopen(output, "wb");
    if (!out) {
        fprintf(stderr, "s32-as: cannot open '%s'\n", output);
        assembler_free(&as);
        return 1;
    }

    bool ok;
    if (elf_format)
        ok = assembler_write_elf_obj(&as, out);
    else
        ok = assembler_write_flat(&as, out);

    fclose(out);

    if (!ok) {
        fprintf(stderr, "s32-as: %s\n", as.error_buf);
        assembler_free(&as);
        return 1;
    }

    assembler_free(&as);
    return 0;
}
