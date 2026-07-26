#ifndef S32CC_PARSER_H
#define S32CC_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "symtable.h"

typedef struct {
    Lexer *lex;
    SymTable *globals;
    SymTable *locals;
    /* Switch tracking for break/continue */
    bool in_switch;
    int break_count;
    int continue_count;
    /* Error handling */
    char error_buf[1024];
    bool has_error;
} Parser;

void parser_init(Parser *p, Lexer *lex);
ASTNode *parser_parse(Parser *p);

#endif
