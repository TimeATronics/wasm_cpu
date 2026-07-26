#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *input = "../refs/writing-a-c-compiler-tests/tests/chapter_3/invalid_parse/malformed_paren.c";
    FILE *in = fopen(input, "rb");
    if (!in) { fprintf(stderr, "cannot open %s\n", input); return 1; }

    Lexer lexer;
    lexer_init(&lexer, input, in);
    Parser parser;
    parser_init(&parser, &lexer);
    ASTNode *ast = parser_parse(&parser);
    fprintf(stderr, "DEBUG: has_error=%d error_buf='%s'\n", parser.has_error, parser.error_buf);
    if (parser.has_error) {
        fprintf(stderr, "s32-cc: %s\n", parser.error_buf);
        exit(2);
    }
    fclose(in);

    FILE *out = fopen("test.bin", "wb");
    Codegen cg;
    codegen_init(&cg);
    codegen_program(&cg, ast, out);
    codegen_free(&cg);
    fclose(out);
    return 0;
}
