#ifndef S32CC_LEXER_H
#define S32CC_LEXER_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    /* Literals */
    TOK_INT_LIT, TOK_CHAR_LIT, TOK_STRING_LIT, TOK_FLOAT_LIT, TOK_IDENT,
    /* Keywords */
    TOK_INT, TOK_CHAR, TOK_VOID, TOK_RETURN, TOK_IF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_DO, TOK_SWITCH, TOK_CASE, TOK_DEFAULT,
    TOK_BREAK, TOK_CONTINUE, TOK_GOTO, TOK_SIZEOF, TOK_NULL,
    TOK_UNSIGNED, TOK_SIGNED, TOK_LONG, TOK_SHORT,
    TOK_ENUM, TOK_STRUCT, TOK_UNION,
    TOK_TYPEDEF, TOK_STATIC, TOK_EXTERN, TOK_CONST, TOK_VOLATILE,
    TOK_DOUBLE, TOK_FLOAT, TOK_ASM,
    /* Operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_BANG,
    TOK_SHL, TOK_SHR,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_LAND, TOK_LOR,
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_PERCENT_EQ, TOK_AMP_EQ, TOK_PIPE_EQ, TOK_CARET_EQ,
    TOK_SHL_EQ, TOK_SHR_EQ,
    TOK_INC, TOK_DEC,
    /* Punctuation */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET, TOK_SEMICOLON, TOK_COMMA,
    TOK_DOT, TOK_ARROW, TOK_COLON, TOK_QUESTION, TOK_ELLIPSIS,
    /* Special */
    TOK_EOF, TOK_ERROR,
} TokenKind;

typedef struct {
    TokenKind kind;
    int line, col;
    bool is_long_lit;
    union {
        int int_val;
        char char_val;
        char *str_val;
        double double_val;
        int64_t long_val;
    } val;
} Token;

typedef struct {
    FILE *fp;
    const char *filename;
    char *buf;
    size_t buf_len, buf_pos;
    int line, col;
    Token cur;
    bool has_cur;
    /* Preprocessor state */
    char *include_dirs[64];
    int include_dir_count;
    int skip_depth;
    bool skipping;
} Lexer;

void lexer_init(Lexer *lex, const char *filename, FILE *fp);
void lexer_init_string(Lexer *lex, const char *src);
Token lexer_next(Lexer *lex);
Token lexer_peek(Lexer *lex);
const char *token_name(TokenKind kind);

#endif
