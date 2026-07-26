#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -- Macro table for #define ------------------------------------- */

typedef struct Macro {
    char *name;
    char *value;
    struct Macro *next;
} Macro;

static Macro *macro_list = NULL;

static void macro_add(const char *name, const char *value) {
    for (Macro *m = macro_list; m; m = m->next) {
        if (strcmp(m->name, name) == 0) { free(m->value); m->value = strdup(value); return; }
    }
    Macro *m = malloc(sizeof(Macro));
    m->name = strdup(name);
    m->value = strdup(value);
    m->next = macro_list;
    macro_list = m;
}

static const char *macro_get(const char *name) {
    for (Macro *m = macro_list; m; m = m->next)
        if (strcmp(m->name, name) == 0) return m->value;
    return NULL;
}

/* -- Lexer state ------------------------------------------------- */

void lexer_init(Lexer *lex, const char *filename, FILE *fp) {
    memset(lex, 0, sizeof(*lex));
    lex->fp = fp;
    lex->filename = filename;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    lex->buf = malloc(sz + 1);
    lex->buf_len = fread(lex->buf, 1, sz, fp);
    lex->buf[lex->buf_len] = 0;
    lex->buf_pos = 0;
    lex->line = 1;
    lex->col = 1;
}

void lexer_init_string(Lexer *lex, const char *src) {
    memset(lex, 0, sizeof(*lex));
    lex->filename = "<string>";
    lex->buf_len = strlen(src);
    lex->buf = malloc(lex->buf_len + 1);
    memcpy(lex->buf, src, lex->buf_len + 1);
    lex->buf_pos = 0;
    lex->line = 1;
    lex->col = 1;
}

static char peek_ch(Lexer *lex) {
    if (lex->buf_pos >= lex->buf_len) return 0;
    return lex->buf[lex->buf_pos];
}

static char next_ch(Lexer *lex) {
    if (lex->buf_pos >= lex->buf_len) return 0;
    char c = lex->buf[lex->buf_pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return c;
}

/* Process a #define directive. Returns true if it was handled. */
static bool process_define(Lexer *lex) {
    /* Read the rest of the line after #define */
    /* Find start of macro name */
    while (peek_ch(lex) && isspace((unsigned char)peek_ch(lex)) && peek_ch(lex) != '\n')
        next_ch(lex);
    
    if (!isalpha((unsigned char)peek_ch(lex)) && peek_ch(lex) != '_')
        return false; /* Not a valid macro name */
    
    /* Read macro name */
    char name[256];
    int ni = 0;
    while (peek_ch(lex) && (isalnum((unsigned char)peek_ch(lex)) || peek_ch(lex) == '_')) {
        name[ni++] = next_ch(lex);
        if (ni >= 255) break;
    }
    name[ni] = 0;
    
    /* Skip whitespace before value */
    while (peek_ch(lex) && isspace((unsigned char)peek_ch(lex)) && peek_ch(lex) != '\n')
        next_ch(lex);
    
    /* Read rest of line as the value */
    char value[4096];
    int vi = 0;
    while (peek_ch(lex) && peek_ch(lex) != '\n' && peek_ch(lex) != '\r') {
        value[vi++] = next_ch(lex);
        if (vi >= 4095) break;
    }
    /* Trim trailing whitespace from value */
    while (vi > 0 && isspace((unsigned char)value[vi-1])) vi--;
    value[vi] = 0;
    
    macro_add(name, value);
    return true;
}

static void skip_whitespace_and_comments(Lexer *lex) {
    for (;;) {
        char c = peek_ch(lex);
        if (c == 0) break;
        if (isspace(c)) { next_ch(lex); continue; }
        if (c == '/' && lex->buf_pos + 1 < lex->buf_len) {
            char n = lex->buf[lex->buf_pos + 1];
            if (n == '/') {
                while (peek_ch(lex) && peek_ch(lex) != '\n') next_ch(lex);
                continue;
            }
            if (n == '*') {
                next_ch(lex); next_ch(lex);
                while (peek_ch(lex)) {
                    if (peek_ch(lex) == '*' && lex->buf_pos + 1 < lex->buf_len && lex->buf[lex->buf_pos + 1] == '/') {
                        next_ch(lex); next_ch(lex);
                        break;
                    }
                    next_ch(lex);
                }
                continue;
            }
        }
        if (c == '#' && lex->col == 1) {
            next_ch(lex); /* consume '#' */
            /* Skip whitespace after # */
            while (peek_ch(lex) && isspace((unsigned char)peek_ch(lex)) && peek_ch(lex) != '\n')
                next_ch(lex);
            
            if (peek_ch(lex) == 'd' && lex->buf_pos + 5 < lex->buf_len &&
                memcmp(lex->buf + lex->buf_pos, "define", 6) == 0) {
                /* Skip "define" */
                for (int i = 0; i < 6; i++) next_ch(lex);
                process_define(lex);
                /* Skip rest of line */
                while (peek_ch(lex) && peek_ch(lex) != '\n') next_ch(lex);
                continue;
            }
            
            if (peek_ch(lex) == 'i' && lex->buf_pos + 6 < lex->buf_len &&
                memcmp(lex->buf + lex->buf_pos, "include", 7) == 0) {
                /* Skip 'include' */
                for (int i = 0; i < 7; i++) next_ch(lex);
                /* Skip whitespace */
                while (peek_ch(lex) && isspace((unsigned char)peek_ch(lex)) && peek_ch(lex) != '\n')
                    next_ch(lex);
                
                char delim = peek_ch(lex);
                if (delim == '"' || delim == '<') {
                    char end_delim = (delim == '"') ? '"' : '>';
                    next_ch(lex); /* skip opening delim */
                    
                    char fname[512]; int fi = 0;
                    while (peek_ch(lex) && peek_ch(lex) != end_delim && peek_ch(lex) != '\n') {
                        fname[fi++] = next_ch(lex);
                    }
                    fname[fi] = 0;
                    if (peek_ch(lex) == end_delim) next_ch(lex);
                    
                    /* Try to open and include the file */
                    char path[1024];
                    FILE *inc = NULL;
                    if (delim == '"') {
                        /* Try relative to source file directory */
                        const char *slash = strrchr(lex->filename, '/');
                        if (!slash) slash = strrchr(lex->filename, '\\');
                        if (slash) {
                            int dlen = (int)(slash - lex->filename);
                            memcpy(path, lex->filename, dlen);
                            snprintf(path + dlen, sizeof(path) - dlen, "/%s", fname);
                        } else {
                            snprintf(path, sizeof(path), "%s", fname);
                        }
                        inc = fopen(path, "r");
                    }
                    if (!inc) inc = fopen(fname, "r");
                    
                    if (inc) {
                        /* Save current lexer state */
                        char *old_buf = lex->buf;
                        size_t old_len = lex->buf_len;
                        size_t old_pos = lex->buf_pos;
                        int old_line = lex->line;
                        int old_col = lex->col;
                        
                        /* Read the included file, strip \r */
                        fseek(inc, 0, SEEK_END);
                        long sz = ftell(inc);
                        fseek(inc, 0, SEEK_SET);
                        char *new_buf = malloc(sz + 1);
                        fread(new_buf, 1, sz, inc);
                        new_buf[sz] = 0;
                        fclose(inc);
                        
                        /* Strip \r from included content */
                        int clean_len = 0;
                        for (long i = 0; i < sz; i++)
                            if (new_buf[i] != '\r') new_buf[clean_len++] = new_buf[i];
                        new_buf[clean_len] = 0;
                        
                        /* Concatenate included content + rest of current file */
                        size_t rest_len = old_len - old_pos;
                        char *combined = malloc(clean_len + 1 + rest_len + 1);
                        memcpy(combined, new_buf, clean_len);
                        combined[clean_len] = '\n'; /* separator */
                        memcpy(combined + clean_len + 1, old_buf + old_pos, rest_len);
                        combined[clean_len + 1 + rest_len] = 0;
                        
                        free(new_buf);
                        
                        /* Replace buffer and reset position */
                        lex->buf = combined;
                        lex->buf_len = clean_len + 1 + rest_len;
                        lex->buf_pos = 0;
                        lex->line = 1;
                        lex->col = 1;
                        
                        free(old_buf);
                        continue;
                    }
                }
                /* Skip rest of line if include failed */
                while (peek_ch(lex) && peek_ch(lex) != '\n') next_ch(lex);
                continue;
            }
            
            /* Any other #directive: skip the line */
            while (peek_ch(lex) && peek_ch(lex) != '\n') next_ch(lex);
            continue;
        }
        break;
    }
}

static Token make_token(TokenKind kind, int line, int col) {
    Token t = {0};
    t.kind = kind;
    t.line = line;
    t.col = col;
    return t;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Token lexer_next(Lexer *lex) {
    skip_whitespace_and_comments(lex);
    char c = peek_ch(lex);
    if (c == 0) return make_token(TOK_EOF, lex->line, lex->col);

    int sl = lex->line, sc = lex->col;

    /* String literal */
    if (c == '"') {
        next_ch(lex);
        char *buf = malloc(256);
        int len = 0;
        while (peek_ch(lex) && peek_ch(lex) != '"') {
            char ch = next_ch(lex);
            if (ch == '\\') {
                char esc = next_ch(lex);
                switch (esc) {
                    case 'n': ch = '\n'; break;
                    case 'r': ch = '\r'; break;
                    case 't': ch = '\t'; break;
                    case '0': ch = 0; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    default: ch = esc; break;
                }
            }
            buf[len++] = ch;
        }
        if (peek_ch(lex) == '"') next_ch(lex);
        buf[len] = 0;
        Token t = make_token(TOK_STRING_LIT, sl, sc);
        t.val.str_val = buf;
        return t;
    }

    /* Char literal */
    if (c == '\'') {
        next_ch(lex);
        char ch = next_ch(lex);
        if (ch == '\\') {
            char esc = next_ch(lex);
            switch (esc) {
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case '0': ch = 0; break;
                case '\\': ch = '\\'; break;
                case '\'': ch = '\''; break;
                default: ch = esc; break;
            }
        }
        if (peek_ch(lex) == '\'') next_ch(lex);
        Token t = make_token(TOK_CHAR_LIT, sl, sc);
        t.val.char_val = ch;
        return t;
    }

    /* Number (integer or float) */
    if (isdigit(c) || (c == '0' && lex->buf_pos + 1 < lex->buf_len &&
        (lex->buf[lex->buf_pos + 1] == 'x' || lex->buf[lex->buf_pos + 1] == 'X'))
        || c == '.') {
        
        /* Check for float literal (starts with digit or digit+'.' or '.'+digit) */
        bool is_float = (c == '.');
        
        if (c == '0' && (peek_ch(lex) == 'x' || peek_ch(lex) == 'X')) {
            next_ch(lex); next_ch(lex);
            unsigned long val = 0;
            while (isxdigit(peek_ch(lex)))
                val = val * 16 + hex_val(next_ch(lex));
            if (peek_ch(lex) == 'u' || peek_ch(lex) == 'U') next_ch(lex);
            if (peek_ch(lex) == 'l' || peek_ch(lex) == 'L') next_ch(lex);
            Token t = make_token(TOK_INT_LIT, sl, sc);
            t.val.int_val = (int)val;
            return t;
        }
        
        if (c >= '0' && c <= '9') {
            /* Integer part - consume first digit via next_ch, then read rest */
            char buf[64]; int bi = 0;
            buf[bi++] = next_ch(lex);
            while (isdigit(peek_ch(lex)) && bi < 60)
                buf[bi++] = next_ch(lex);
            /* Check for decimal point */
            if (peek_ch(lex) == '.') {
                is_float = true;
                buf[bi++] = next_ch(lex);
                while (isdigit(peek_ch(lex)) && bi < 60)
                    buf[bi++] = next_ch(lex);
            }
            /* Check for exponent */
            if (peek_ch(lex) == 'e' || peek_ch(lex) == 'E') {
                is_float = true;
                buf[bi++] = next_ch(lex);
                if (peek_ch(lex) == '+' || peek_ch(lex) == '-')
                    buf[bi++] = next_ch(lex);
                while (isdigit(peek_ch(lex)) && bi < 60)
                    buf[bi++] = next_ch(lex);
            }
            buf[bi] = '\0';
            
            if (is_float) {
                double fval = strtod(buf, NULL);
                if (peek_ch(lex) == 'f' || peek_ch(lex) == 'F') next_ch(lex);
                if (peek_ch(lex) == 'l' || peek_ch(lex) == 'L') next_ch(lex);
                Token t = make_token(TOK_FLOAT_LIT, sl, sc);
                t.val.double_val = fval;
                return t;
            }
            
            /* Integer literal with possible suffix */
            bool is_long = false;
            if (peek_ch(lex) == 'u' || peek_ch(lex) == 'U') next_ch(lex);
            if (peek_ch(lex) == 'l' || peek_ch(lex) == 'L') { next_ch(lex); is_long = true; }
            if (!is_long && (peek_ch(lex) == 'l' || peek_ch(lex) == 'L')) { next_ch(lex); is_long = true; }
            Token t = make_token(TOK_INT_LIT, sl, sc);
            if (is_long) {
                t.is_long_lit = true;
                t.val.long_val = strtoll(buf, NULL, 10);
            } else {
                t.val.int_val = (int)strtoul(buf, NULL, 10);
            }
            return t;
        }
        
        /* Starts with '.' - float like ".5" - only if followed by digit */
        if (c == '.' && isdigit(peek_ch(lex))) {
            is_float = true;
            char buf[64]; int bi = 0;
            buf[bi++] = next_ch(lex);
            while (isdigit(peek_ch(lex)) && bi < 60)
                buf[bi++] = next_ch(lex);
            if ((peek_ch(lex) == 'e' || peek_ch(lex) == 'E') && bi < 58) {
                buf[bi++] = next_ch(lex);
                if (peek_ch(lex) == '+' || peek_ch(lex) == '-')
                    buf[bi++] = next_ch(lex);
                while (isdigit(peek_ch(lex)) && bi < 60)
                    buf[bi++] = next_ch(lex);
            }
            buf[bi] = 0;
            double fval = strtod(buf, NULL);
            if (peek_ch(lex) == 'f' || peek_ch(lex) == 'F') next_ch(lex);
            if (peek_ch(lex) == 'l' || peek_ch(lex) == 'L') next_ch(lex);
            Token t = make_token(TOK_FLOAT_LIT, sl, sc);
            t.val.double_val = fval;
            return t;
        }
    }

    /* Identifier or keyword */
    if (isalpha(c) || c == '_') {
        char *id = malloc(64);
        int len = 0;
        do {
            id[len++] = next_ch(lex);
        } while ((isalnum(peek_ch(lex)) || peek_ch(lex) == '_') && len < 60);
        id[len] = 0;

        /* Check for macro expansion */
        const char *macro_val = macro_get(id);
        if (macro_val) {
            /* Save current state, insert macro value, re-tokenize */
            free(id);
            
            char *old_buf = lex->buf;
            size_t old_len = lex->buf_len;
            size_t old_pos = lex->buf_pos;
            
            int vlen = strlen(macro_val);
            size_t rest_len = old_len - old_pos;
            char *combined = malloc(vlen + 1 + rest_len + 1);
            memcpy(combined, macro_val, vlen);
            combined[vlen] = ' ';
            memcpy(combined + vlen + 1, old_buf + old_pos, rest_len);
            combined[vlen + 1 + rest_len] = 0;
            
            lex->buf = combined;
            lex->buf_len = vlen + 1 + rest_len;
            lex->buf_pos = 0;
            /* Keep line/col - macro expansion doesn't change source location */
            
            free(old_buf);
            /* Return the first token from the expanded buffer */
            return lexer_next(lex);
        }
        
        TokenKind kind = TOK_IDENT;
        if (len == 3 && memcmp(id, "int", 3) == 0) kind = TOK_INT;
        else if (len == 4 && memcmp(id, "char", 4) == 0) kind = TOK_CHAR;
        else if (len == 4 && memcmp(id, "void", 4) == 0) kind = TOK_VOID;
        else if (len == 6 && memcmp(id, "return", 6) == 0) kind = TOK_RETURN;
        else if (len == 2 && memcmp(id, "if", 2) == 0) kind = TOK_IF;
        else if (len == 4 && memcmp(id, "else", 4) == 0) kind = TOK_ELSE;
        else if (len == 5 && memcmp(id, "while", 5) == 0) kind = TOK_WHILE;
        else if (len == 3 && memcmp(id, "for", 3) == 0) kind = TOK_FOR;
        else if (len == 2 && memcmp(id, "do", 2) == 0) kind = TOK_DO;
        else if (len == 6 && memcmp(id, "switch", 6) == 0) kind = TOK_SWITCH;
        else if (len == 4 && memcmp(id, "case", 4) == 0) kind = TOK_CASE;
        else if (len == 7 && memcmp(id, "default", 7) == 0) kind = TOK_DEFAULT;
        else if (len == 5 && memcmp(id, "break", 5) == 0) kind = TOK_BREAK;
        else if (len == 8 && memcmp(id, "continue", 8) == 0) kind = TOK_CONTINUE;
        else if (len == 4 && memcmp(id, "goto", 4) == 0) kind = TOK_GOTO;
        else if (len == 6 && memcmp(id, "sizeof", 6) == 0) kind = TOK_SIZEOF;
        else if (len == 4 && memcmp(id, "NULL", 4) == 0) kind = TOK_NULL;
        else if (len == 8 && memcmp(id, "unsigned", 8) == 0) kind = TOK_UNSIGNED;
        else if (len == 6 && memcmp(id, "signed", 6) == 0) kind = TOK_SIGNED;
        else if (len == 4 && memcmp(id, "long", 4) == 0) kind = TOK_LONG;
        else if (len == 5 && memcmp(id, "short", 5) == 0) kind = TOK_SHORT;
        else if (len == 4 && memcmp(id, "enum", 4) == 0) kind = TOK_ENUM;
        else if (len == 6 && memcmp(id, "struct", 6) == 0) kind = TOK_STRUCT;
        else if (len == 5 && memcmp(id, "union", 5) == 0) kind = TOK_UNION;
        else if (len == 7 && memcmp(id, "typedef", 7) == 0) kind = TOK_TYPEDEF;
        else if (len == 6 && memcmp(id, "static", 6) == 0) kind = TOK_STATIC;
        else if (len == 6 && memcmp(id, "extern", 6) == 0) kind = TOK_EXTERN;
        else if (len == 5 && memcmp(id, "const", 5) == 0) kind = TOK_CONST;
        else if (len == 8 && memcmp(id, "volatile", 8) == 0) kind = TOK_VOLATILE;
        else if (len == 6 && memcmp(id, "double", 6) == 0) kind = TOK_DOUBLE;
        else if (len == 5 && memcmp(id, "float", 5) == 0) kind = TOK_FLOAT;
        else if (len == 3 && memcmp(id, "asm", 3) == 0) kind = TOK_ASM;
        if (kind != TOK_IDENT) { free(id); }
        Token t = make_token(kind, sl, sc);
        if (kind == TOK_IDENT || kind == TOK_NULL) t.val.str_val = id;
        else if (kind == TOK_NULL) t.val.int_val = 0;
        return t;
    }

    /* Operators */
    next_ch(lex);
    char n = peek_ch(lex);
    TokenKind kind = TOK_ERROR;
    switch (c) {
        case '+': kind = (n == '+') ? (next_ch(lex), TOK_INC) :
                         (n == '=') ? (next_ch(lex), TOK_PLUS_EQ) : TOK_PLUS; break;
        case '-': kind = (n == '-') ? (next_ch(lex), TOK_DEC) :
                         (n == '=') ? (next_ch(lex), TOK_MINUS_EQ) :
                         (n == '>') ? (next_ch(lex), TOK_ARROW) : TOK_MINUS; break;
        case '*': kind = (n == '=') ? (next_ch(lex), TOK_STAR_EQ) : TOK_STAR; break;
        case '/': kind = (n == '=') ? (next_ch(lex), TOK_SLASH_EQ) : TOK_SLASH; break;
        case '%': kind = (n == '=') ? (next_ch(lex), TOK_PERCENT_EQ) : TOK_PERCENT; break;
        case '&': kind = (n == '&') ? (next_ch(lex), TOK_LAND) :
                         (n == '=') ? (next_ch(lex), TOK_AMP_EQ) : TOK_AMP; break;
        case '|': kind = (n == '|') ? (next_ch(lex), TOK_LOR) :
                         (n == '=') ? (next_ch(lex), TOK_PIPE_EQ) : TOK_PIPE; break;
        case '^': kind = (n == '=') ? (next_ch(lex), TOK_CARET_EQ) : TOK_CARET; break;
        case '~': kind = TOK_TILDE; break;
        case '!': kind = (n == '=') ? (next_ch(lex), TOK_NEQ) : TOK_BANG; break;
        case '<': kind = (n == '<') ? (next_ch(lex), (peek_ch(lex) == '=' ? (next_ch(lex), TOK_SHL_EQ) : TOK_SHL)) :
                         (n == '=') ? (next_ch(lex), TOK_LTE) : TOK_LT; break;
        case '>': kind = (n == '>') ? (next_ch(lex), (peek_ch(lex) == '=' ? (next_ch(lex), TOK_SHR_EQ) : TOK_SHR)) :
                         (n == '=') ? (next_ch(lex), TOK_GTE) : TOK_GT; break;
        case '=': kind = (n == '=') ? (next_ch(lex), TOK_EQ) : TOK_ASSIGN; break;
        case '(': kind = TOK_LPAREN; break;
        case ')': kind = TOK_RPAREN; break;
        case '{': kind = TOK_LBRACE; break;
        case '}': kind = TOK_RBRACE; break;
        case '[': kind = TOK_LBRACKET; break;
        case ']': kind = TOK_RBRACKET; break;
        case ';': kind = TOK_SEMICOLON; break;
        case ',': kind = TOK_COMMA; break;
        case '.': {
            if (n == '.' && lex->buf_pos + 2 < lex->buf_len && lex->buf[lex->buf_pos + 2] == '.') {
                next_ch(lex); next_ch(lex);
                kind = TOK_ELLIPSIS;
            } else {
                kind = TOK_DOT;
            }
            break;
        }
        case ':': kind = TOK_COLON; break;
        case '?': kind = TOK_QUESTION; break;
        default: kind = TOK_ERROR; break;
    }
    Token t = make_token(kind, sl, sc);
    return t;
}

Token lexer_peek(Lexer *lex) {
    if (lex->has_cur) return lex->cur;
    lex->cur = lexer_next(lex);
    lex->has_cur = true;
    return lex->cur;
}

const char *token_name(TokenKind kind) {
    switch (kind) {
        case TOK_INT_LIT: return "int_literal";
        case TOK_CHAR_LIT: return "char_literal";
        case TOK_FLOAT_LIT: return "float_literal";
        case TOK_STRING_LIT: return "string_literal";
        case TOK_IDENT: return "identifier";
        case TOK_INT: return "int";
        case TOK_CHAR: return "char";
        case TOK_VOID: return "void";
        case TOK_RETURN: return "return";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_WHILE: return "while";
        case TOK_FOR: return "for";
        case TOK_DO: return "do";
        case TOK_SWITCH: return "switch";
        case TOK_CASE: return "case";
        case TOK_DEFAULT: return "default";
        case TOK_BREAK: return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_GOTO: return "goto";
        case TOK_SIZEOF: return "sizeof";
        case TOK_NULL: return "NULL";
        case TOK_UNSIGNED: return "unsigned";
        case TOK_SIGNED: return "signed";
        case TOK_LONG: return "long";
        case TOK_SHORT: return "short";
        case TOK_ENUM: return "enum";
        case TOK_STRUCT: return "struct";
        case TOK_UNION: return "union";
        case TOK_TYPEDEF: return "typedef";
        case TOK_STATIC: return "static";
        case TOK_EXTERN: return "extern";
        case TOK_CONST: return "const";
        case TOK_VOLATILE: return "volatile";
        case TOK_DOUBLE: return "double";
        case TOK_FLOAT: return "float";
        case TOK_STAR: return "'*'";
        case TOK_PLUS: return "'+'";
        case TOK_MINUS: return "'-'";
        case TOK_SLASH: return "'/'";
        case TOK_PERCENT: return "'%'";
        case TOK_AMP: return "'&'";
        case TOK_PIPE: return "'|'";
        case TOK_CARET: return "'^'";
        case TOK_TILDE: return "'~'";
        case TOK_BANG: return "'!'";
        case TOK_SHL: return "'<<'";
        case TOK_SHR: return "'>>'";
        case TOK_EQ: return "'=='";
        case TOK_NEQ: return "'!='";
        case TOK_LT: return "'<'";
        case TOK_GT: return "'>'";
        case TOK_LTE: return "'<='";
        case TOK_GTE: return "'>='";
        case TOK_LAND: return "'&&'";
        case TOK_LOR: return "'||'";
        case TOK_ASSIGN: return "'='";
        case TOK_PLUS_EQ: return "'+='";
        case TOK_MINUS_EQ: return "'-='";
        case TOK_STAR_EQ: return "'*='";
        case TOK_SLASH_EQ: return "'/='";
        case TOK_PERCENT_EQ: return "'%='";
        case TOK_AMP_EQ: return "'&='";
        case TOK_PIPE_EQ: return "'|='";
        case TOK_CARET_EQ: return "'^='";
        case TOK_SHL_EQ: return "'<<='";
        case TOK_SHR_EQ: return "'>>='";
        case TOK_INC: return "'++'";
        case TOK_DEC: return "'--'";
        case TOK_ARROW: return "'->'";
        case TOK_DOT: return "'.'";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_SEMICOLON: return "';'";
        case TOK_COMMA: return "','";
        case TOK_COLON: return "':'";
        case TOK_QUESTION: return "'?'";
        case TOK_EOF: return "EOF";
        case TOK_ERROR: return "error";
        default: return "token";
    }
}
