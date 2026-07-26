#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-S] [-c] [-o output] input.c\n", prog);
    fprintf(stderr, "  -S        Output assembly text instead of binary\n");
    fprintf(stderr, "  -c        Produce ELF object file (.o)\n");
    fprintf(stderr, "  -o output Output file (default: stdout)\n");
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    bool asm_output = false;
    bool object_output = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) asm_output = true;
        else if (strcmp(argv[i], "-c") == 0) object_output = true;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output = argv[++i];
        else if (argv[i][0] != '-') input = argv[i];
        else { usage(argv[0]); return 1; }
    }

    if (!input) { usage(argv[0]); return 1; }

    FILE *in = fopen(input, "r");
    if (!in) { fprintf(stderr, "cannot open: %s\n", input); return 1; }

    FILE *out = stdout;
    if (output) {
        out = fopen(output, "wb");
        if (!out) { fprintf(stderr, "cannot open output: %s\n", output); return 1; }
    }

    Lexer lex;
    lexer_init(&lex, input, in);

    Parser parser;
    parser_init(&parser, &lex);
    ASTNode *ast = parser_parse(&parser);
    if (parser.has_error) {
        fprintf(stderr, "s32-cc: %s\n", parser.error_buf);
        exit(2);
    }

    if (parser.has_error) {
        fprintf(stderr, "parse error: %s\n", parser.error_buf);
        return 1;
    }

    if (asm_output) {
        fprintf(out, "; assembly output not yet implemented\n");
    } else {
        Codegen cg;
        codegen_init(&cg);
        if (object_output) {
            codegen_program_object(&cg, ast, out);
        } else {
            codegen_program(&cg, ast, out);
        }
        codegen_free(&cg);
    }

    if (out != stdout) fclose(out);
    fclose(in);
    return 0;
}
