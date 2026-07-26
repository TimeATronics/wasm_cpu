#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations for tag and typedef functions */
typedef struct TagEntry TagEntry;
static TagEntry *tags;
static void register_tag(const char *name, Type *type);
static Type *lookup_tag(const char *name);
typedef struct TypedefEntry TypedefEntry;
static TypedefEntry *typedefs;
static void typedef_add(const char *name, Type *type);
static Type *typedef_lookup(const char *name);
static ASTNode *parse_initializer_list(Parser *p, Type *ty, int sl, int sc);

/* Simplified type inference */

/* Compute the common type of two operands (simplified usual arithmetic conversion) */
static Type *common_type(Type *a, Type *b) {
    if (!a) return b;
    if (!b) return a;
    /* Both integer types */
    if (a->kind == TYPE_CHAR || a->kind == TYPE_INT ||
        a->kind == TYPE_LONG || a->kind == TYPE_ENUM) {
        if (b->kind == TYPE_CHAR || b->kind == TYPE_INT ||
            b->kind == TYPE_LONG || b->kind == TYPE_ENUM) {
            /* Use larger size; if equal, unsigned wins */
            Type *result = (a->size >= b->size) ? a : b;
            if (a->size == b->size && (a->is_unsigned || b->is_unsigned)) {
                Type *t = type_new(result->kind, result->size);
                t->is_unsigned = true;
                return t;
            }
            return result;
        }
        return a; /* a is integer, b is pointer */
    }
    return a;
}

/* Apply type inference to a binary expression node */
static void set_binary_type(ASTNode *node) {
    if (node->kind == AST_BINARY && node->as.binary.left && node->as.binary.right) {
        node->type = common_type(node->as.binary.left->type, node->as.binary.right->type);
    } else if (node->kind == AST_ASSIGN && node->as.assign.lvalue) {
        node->type = node->as.assign.lvalue->type;
    } else if (node->kind == AST_TERNARY) {
        if (node->as.if_.then_body && node->as.if_.else_body)
            node->type = common_type(node->as.if_.then_body->type, node->as.if_.else_body->type);
    }
    if (node->kind == AST_INDEX) {
        if (node->as.index.base && node->as.index.base->type &&
            node->as.index.base->type->kind == TYPE_ARRAY)
            node->type = node->as.index.base->type->base;
    }
    if (node->kind == AST_DEREF) {
        if (node->as.unary.expr && node->as.unary.expr->type &&
            node->as.unary.expr->type->kind == TYPE_PTR)
            node->type = node->as.unary.expr->type->base;
    }
}

/* chibicc-style context for loops/switch */
static ASTNode *current_switch = NULL;
static char *brk_label = NULL;
static char *cont_label = NULL;
static int label_counter = 0;

static char *new_unique_name(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), ".L..%d", label_counter++);
    return strdup(buf);
}

static void parser_error(Parser *p, const char *msg) {
    if (!p->has_error) {
        snprintf(p->error_buf, sizeof(p->error_buf), "line %d col %d: %s",
                 p->lex->line, p->lex->col, msg);
        p->has_error = true;
    }
}

static Token peek(Parser *p) { return lexer_peek(p->lex); }

static Token consume(Parser *p) {
    if (p->lex->has_cur) {
        p->lex->has_cur = false;
        return p->lex->cur;
    }
    return lexer_next(p->lex);
}

static Token expect(Parser *p, TokenKind kind) {
    Token t = peek(p);
    if (t.kind != kind) {
        char buf[256];
        snprintf(buf, sizeof(buf), "expected '%s', got '%s'", token_name(kind), token_name(t.kind));
        parser_error(p, buf);
    }
    return consume(p);
}

static bool check(Parser *p, TokenKind kind) {
    return peek(p).kind == kind;
}

/* Peek at the token after the current one without consuming */
static Token peek_next(Parser *p) {
    bool saved_has_cur = p->lex->has_cur;
    Token saved_cur = p->lex->cur;
    size_t saved_pos = p->lex->buf_pos;
    int saved_line = p->lex->line;
    int saved_col = p->lex->col;

    p->lex->has_cur = false;
    Token next = lexer_peek(p->lex);

    p->lex->has_cur = saved_has_cur;
    p->lex->cur = saved_cur;
    p->lex->buf_pos = saved_pos;
    p->lex->line = saved_line;
    p->lex->col = saved_col;

    return next;
}

/* Forward declarations */
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_expr_with_bp(Parser *p, int min_bp);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block_item(Parser *p);
static ASTNode *parse_decl(Parser *p, SymTable *st);
static ASTNode *parse_initializer_list(Parser *p, Type *ty, int sl, int sc);

/* Parse a type specifier - chibicc-style bit-counter approach */
static Type *parse_type(Parser *p) {
    bool is_void = false, is_char = false, is_int = false;
    bool is_long = false, is_short = false;
    bool is_unsigned = false, is_signed = false;
    bool is_enum = false;

    Type *base = NULL;

    /* Collect type specifiers (including storage class which can interleave) */
    bool is_double = false, is_float = false, is_struct = false, is_union = false;
    char *struct_tag = NULL;
    
    while (true) {
        TokenKind k = peek(p).kind;
        if (k == TOK_VOID && !is_void) { consume(p); is_void = true; }
        else if (k == TOK_CHAR && !is_char && !is_long && !is_short) { consume(p); is_char = true; }
        else if (k == TOK_INT && !is_int) { consume(p); is_int = true; }
        else if (k == TOK_LONG && !is_long) { consume(p); is_long = true; }
        /* TODO: long long should be 64-bit on LP64 platforms.
         * We treat it as 32-bit (same as long) because our target is 32-bit.
         * This works for now but is incorrect for 64-bit arithmetic. */
        else if (k == TOK_LONG && is_long) { consume(p); }
        else if (k == TOK_SHORT && !is_short) { consume(p); is_short = true; }
        else if (k == TOK_SHORT && is_short) { consume(p); /* short short → ignore */ }
        else if (k == TOK_UNSIGNED && !is_unsigned) { consume(p); is_unsigned = true; }
        else if (k == TOK_SIGNED && !is_signed) { consume(p); is_signed = true; }
        else if (k == TOK_ENUM && !is_enum) { consume(p); is_enum = true; break; }
        else if (k == TOK_DOUBLE && !is_double) { consume(p); is_double = true; break; }
        else if (k == TOK_FLOAT && !is_float) { consume(p); is_float = true; break; }
        else if (k == TOK_STRUCT && !is_struct && !is_union) { consume(p); is_struct = true; if (check(p, TOK_IDENT)) struct_tag = consume(p).val.str_val; break; }
        else if (k == TOK_UNION && !is_union && !is_struct) { consume(p); is_union = true; if (check(p, TOK_IDENT)) struct_tag = consume(p).val.str_val; break; }
        else if (k == TOK_CONST || k == TOK_VOLATILE) { consume(p); } /* type qualifiers - accepted, stored */
        else if (k == TOK_IDENT) {
            /* Check for typedef name */
            Type *tdef = typedef_lookup(peek(p).val.str_val);
            if (tdef) {
                consume(p);
                base = tdef;
                break;
            }
            break;
        }
        else break;
    }

    if (is_struct || is_union) {
        bool has_body = check(p, TOK_LBRACE);
        if (has_body) {
            consume(p);
            base = is_struct ? type_struct() : type_union();
            Member head = {0}, *cur = &head;
            int max_align = 1;
            while (!check(p, TOK_RBRACE)) {
                /* WORKAROUND: Anonymous struct/union members (C11 feature).
                 * We flatten the anonymous member's sub-members into the parent struct.
                 * This works for single-level nesting but NOT for multi-level
                 * anonymous structs within anonymous structs. */
                if (check(p, TOK_STRUCT) || check(p, TOK_UNION)) {
                    Type *mty = parse_type(p);
                    if (check(p, TOK_SEMICOLON)) consume(p);
                    /* Add all sub-members at current offset */
                    if (mty && (mty->kind == TYPE_STRUCT || mty->kind == TYPE_UNION)) {
                        for (Member *sm = mty->members; sm; sm = sm->next) {
                            Member *m = member_new(sm->name, sm->type);
                            if (is_struct) {
                                m->offset = base->size + sm->offset;
                            } else {
                                m->offset = sm->offset;
                            }
                            cur->next = m; cur = m;
                        }
                        if (is_struct) base->size += type_sizeof(mty);
                        else {
                            int sz = type_sizeof(mty);
                            if (sz > base->size) base->size = sz;
                        }
                        if (mty->align > max_align) max_align = mty->align;
                    }
                    continue;
                }
                Type *mty = parse_type(p);
                if (check(p, TOK_IDENT)) {
                    char *mname = consume(p).val.str_val;
                    /* Handle array declarator in member: int x[10] */
                    while (check(p, TOK_LBRACKET)) {
                        consume(p);
                        int sz = 0;
                        if (check(p, TOK_INT_LIT)) sz = consume(p).val.int_val;
                        expect(p, TOK_RBRACKET);
                        mty = type_array(mty, sz);
                    }
                    Member *m = member_new(mname, mty);
                    if (is_struct) {
                        int align = mty->align;
                        if (align > max_align) max_align = align;
                        base->size = ((base->size + align - 1) / align) * align;
                        m->offset = base->size;
                        base->size += type_sizeof(mty);
                    } else {
                        m->offset = 0;
                        int sz = type_sizeof(mty);
                        if (sz > base->size) base->size = sz;
                        if (mty->align > max_align) max_align = mty->align;
                    }
                    cur->next = m;
                    cur = m;
                    if (check(p, TOK_SEMICOLON)) consume(p);
                } else {
                    /* Accept unnamed member (for padding) */
                    int sz = type_sizeof(mty);
                    if (is_struct) base->size += sz;
                    else if (sz > base->size) base->size = sz;
                    if (check(p, TOK_SEMICOLON)) consume(p);
                }
            }
            expect(p, TOK_RBRACE);
            /* Final alignment */
            base->align = max_align;
            if (is_struct)
                base->size = ((base->size + max_align - 1) / max_align) * max_align;
            base->members = head.next;
            /* Register tag for later lookups */
            if (struct_tag) register_tag(struct_tag, base);
        } else if (struct_tag) {
            /* Tag reference - look up previously defined type */
            base = lookup_tag(struct_tag);
            if (!base) {
                base = is_struct ? type_struct() : type_union();
                register_tag(struct_tag, base);
            }
        } else {
            parser_error(p, "incomplete struct/union declaration");
            base = type_struct();
        }
    } else if (is_enum) {
        /* Parse enum specifier */
        char *tag = NULL;
        if (check(p, TOK_IDENT)) {
            tag = consume(p).val.str_val;
        }
        if (check(p, TOK_LBRACE)) {
            consume(p);
            base = type_enum();
            int val = 0;
            while (!check(p, TOK_RBRACE)) {
                if (check(p, TOK_IDENT)) {
                    char *name = consume(p).val.str_val;
                    if (check(p, TOK_ASSIGN)) {
                        consume(p);
                        val = parse_expr_with_bp(p, 2)->as.int_val;
                    }
                    Symbol *s = symtable_insert(p->globals, name, type_enum(), SYM_GLOBAL);
                    s->offset = val++;
                    if (check(p, TOK_COMMA)) consume(p);
                } else {
                    parser_error(p, "expected identifier in enum");
                    break;
                }
            }
            expect(p, TOK_RBRACE);
        } else if (tag) {
            base = type_enum();
        } else {
            parser_error(p, "incomplete enum declaration");
            base = type_enum();
        }
    } else if (is_void) {
        base = type_new(TYPE_VOID, 0);
    } else if (is_char) {
        base = type_new(TYPE_CHAR, 1);
        base->is_unsigned = is_unsigned;
    } else if (is_short) {
        base = type_new(TYPE_INT, 2); /* short = 2 bytes (same as int on our word machine) */
        base->is_unsigned = is_unsigned;
    } else if (is_double) {
        base = type_new(TYPE_DOUBLE, 8);
    } else if (is_float) {
        base = type_new(TYPE_DOUBLE, 4); /* treat float as smaller double */
    } else if (is_long) {
        base = type_new(TYPE_LONG, 8); /* 64-bit long = 8 bytes */
        base->is_unsigned = is_unsigned;
    } else {
        /* default: int */
        base = type_new(TYPE_INT, 4);
        base->is_unsigned = is_unsigned;
    }

    /* Parse pointer stars */
    while (check(p, TOK_STAR)) {
        consume(p);
        base = type_ptr(base);
    }
    return base;
}

/* Typedef table */

typedef struct TypedefEntry {
    char *name;
    Type *type;
    struct TypedefEntry *next;
} TypedefEntry;

static TypedefEntry *typedefs = NULL;

static void typedef_add(const char *name, Type *type) {
    TypedefEntry *e = malloc(sizeof(TypedefEntry));
    e->name = strdup(name);
    e->type = type;
    e->next = typedefs;
    typedefs = e;
}

static Type *typedef_lookup(const char *name) {
    for (TypedefEntry *e = typedefs; e; e = e->next)
        if (strcmp(e->name, name) == 0)
            return e->type;
    return NULL;
}

/* Tag namespace (struct/union/enum tags) */

typedef struct TagEntry {
    char *name;
    Type *type;
    struct TagEntry *next;
} TagEntry;

static TagEntry *tags = NULL;

static void register_tag(const char *name, Type *type) {
    TagEntry *e = malloc(sizeof(TagEntry));
    e->name = strdup(name);
    e->type = type;
    e->next = tags;
    tags = e;
}

static Type *lookup_tag(const char *name) {
    for (TagEntry *e = tags; e; e = e->next)
        if (strcmp(e->name, name) == 0)
            return e->type;
    return NULL;
}

/* Parse enum identifier reference (used where enum constants act as values) */
static int lookup_enum_val(Parser *p, const char *name) {
    Symbol *s = symtable_lookup(p->globals, name);
    if (s && s->type && s->type->kind == TYPE_ENUM)
        return s->offset;
    return -1;
}

/* Expression parsing with Pratt precedence */
static int prefix_bp(TokenKind kind) {
    switch (kind) {
        case TOK_MINUS: case TOK_BANG: case TOK_TILDE:
        case TOK_STAR: case TOK_AMP:
        case TOK_INC: case TOK_DEC:
            return 14; /* higher than multiplicative (13) */
        default: return 0;
    }
}

static int infix_bp_left(TokenKind kind) {
    switch (kind) {
        case TOK_COMMA: return 1;
        case TOK_ASSIGN: case TOK_PLUS_EQ: case TOK_MINUS_EQ:
        case TOK_STAR_EQ: case TOK_SLASH_EQ: case TOK_PERCENT_EQ:
        case TOK_AMP_EQ: case TOK_PIPE_EQ: case TOK_CARET_EQ:
        case TOK_SHL_EQ: case TOK_SHR_EQ:
            return 2;
        case TOK_QUESTION: return 3;
        case TOK_LOR: return 4;
        case TOK_LAND: return 5;
        case TOK_PIPE: return 6;
        case TOK_CARET: return 7;
        case TOK_AMP: return 8;
        case TOK_EQ: case TOK_NEQ: return 9;
        case TOK_LT: case TOK_GT: case TOK_LTE: case TOK_GTE: return 10;
        case TOK_SHL: case TOK_SHR: return 11;
        case TOK_PLUS: case TOK_MINUS: return 12;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 13;
        default: return 0;
    }
}

static ASTNode *parse_primary(Parser *p) {
    Token t = peek(p);
    if (t.kind == TOK_INT_LIT) {
        consume(p);
        if (t.is_long_lit) {
            ASTNode *n = ast_int(0, t.line, t.col);
            n->type = type_new(TYPE_LONG, 8);
            n->as.long_val = t.val.long_val;
            return n;
        }
        return ast_int(t.val.int_val, t.line, t.col);
    }
    if (t.kind == TOK_CHAR_LIT) {
        consume(p);
        return ast_char(t.val.char_val, t.line, t.col);
    }
    if (t.kind == TOK_STRING_LIT) {
        consume(p);
        /* Adjacent string literal concatenation: "abc" "def" → "abcdef" */
        char *result = strdup(t.val.str_val ? t.val.str_val : "");
        while (check(p, TOK_STRING_LIT)) {
            Token nt = consume(p);
            if (nt.val.str_val) {
                int old_len = strlen(result);
                result = realloc(result, old_len + strlen(nt.val.str_val) + 1);
                memcpy(result + old_len, nt.val.str_val, strlen(nt.val.str_val) + 1);
            }
        }
        return ast_string(result, t.line, t.col);
    }
    if (t.kind == TOK_FLOAT_LIT) {
        consume(p);
        ASTNode *n = ast_int(0, t.line, t.col);
        n->type = type_new(TYPE_DOUBLE, 8);
        n->as.double_val = t.val.double_val;
        return n;
    }
    if (t.kind == TOK_NULL) {
        consume(p);
        return ast_int(0, t.line, t.col);
    }
    if (t.kind == TOK_IDENT) {
        Token name = consume(p);
        /* Check if this is an enum constant */
        int enum_val = lookup_enum_val(p, name.val.str_val);
        if (enum_val >= 0) {
            return ast_int(enum_val, t.line, t.col);
        }
        if (check(p, TOK_LPAREN)) {
            consume(p);
            ASTNode **args = NULL;
            int arg_count = 0, arg_cap = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    if (arg_count >= arg_cap) {
                        arg_cap = arg_cap ? arg_cap * 2 : 8;
                        args = realloc(args, sizeof(ASTNode*) * arg_cap);
                    }
                    args[arg_count++] = parse_expr_with_bp(p, 2);
                } while (check(p, TOK_COMMA) && (consume(p), 1));
            }
            expect(p, TOK_RPAREN);
            ASTNode *call = ast_call(ast_ident(name.val.str_val, name.line, name.col),
                                       args, arg_count, name.line, name.col);
            /* Set call result type from function's return type */
            Symbol *func_s = symtable_lookup(p->globals, name.val.str_val);
            if (!func_s && p->locals) func_s = symtable_lookup(p->locals, name.val.str_val);
            if (func_s && func_s->type && func_s->type->kind == TYPE_FUNC)
                call->type = func_s->type->base;
            return call;
        }
        ASTNode *id_node = ast_ident(name.val.str_val, name.line, name.col);
        Symbol *id_s = symtable_lookup(p->globals, name.val.str_val);
        if (!id_s && p->locals) id_s = symtable_lookup(p->locals, name.val.str_val);
        if (id_s) id_node->type = id_s->type;
        return id_node;
    }
    if (t.kind == TOK_LPAREN) {
        consume(p);
        /* Check for cast: (type_name)expr */
        TokenKind nk = peek(p).kind;
        if (nk == TOK_INT || nk == TOK_CHAR || nk == TOK_VOID ||
            nk == TOK_LONG || nk == TOK_SHORT ||
            nk == TOK_UNSIGNED || nk == TOK_SIGNED ||
            nk == TOK_ENUM || nk == TOK_DOUBLE || nk == TOK_FLOAT ||
            nk == TOK_STRUCT || nk == TOK_UNION ||
            (nk == TOK_IDENT && typedef_lookup(peek(p).val.str_val))) {
            Type *cty = parse_type(p);
            /* Handle abstract declarators in casts: (int(**)[2]), (int(*)(int)) */
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (check(p, TOK_STAR)) {
                    /* pointer */
                    consume(p);
                    cty = type_ptr(cty);
                    /* check for qualifiers like const - skip for now */
                    while (check(p, TOK_CONST) || check(p, TOK_VOLATILE)) consume(p);
                } else if (check(p, TOK_LPAREN)) {
                    /* Check if this is a nested cast like (int)(double)x */
                    TokenKind nk = peek_next(p).kind;
                    if (nk == TOK_INT || nk == TOK_CHAR || nk == TOK_VOID ||
                        nk == TOK_LONG || nk == TOK_SHORT || 
                        nk == TOK_UNSIGNED || nk == TOK_SIGNED ||
                        nk == TOK_DOUBLE || nk == TOK_FLOAT ||
                        nk == TOK_STRUCT || nk == TOK_UNION) {
                        break; /* nested cast, let expression parser handle it */
                    }
                    /* function params: (int, float) */
                    consume(p);
                    Type **param_types = NULL;
                    int param_count = 0, param_cap = 0;
                    if (!check(p, TOK_RPAREN)) {
                        do {
                            Type *pty = parse_type(p);
                            /* skip param name if present */
                            if (check(p, TOK_IDENT)) consume(p);
                            if (pty->kind == TYPE_VOID && check(p, TOK_RPAREN)) break;
                            if (param_count >= param_cap) {
                                param_cap = param_cap ? param_cap * 2 : 4;
                                param_types = realloc(param_types, sizeof(Type*) * param_cap);
                            }
                            param_types[param_count++] = pty;
                        } while (check(p, TOK_COMMA) && (consume(p), 1));
                    }
                    expect(p, TOK_RPAREN);
                    cty = type_func(cty, param_types, param_count, false);
                    cty->param_count = param_count;
                } else if (check(p, TOK_LBRACKET)) {
                    /* array: [size] or [] */
                    consume(p);
                    int sz = 0;
                    if (check(p, TOK_INT_LIT)) sz = consume(p).val.int_val;
                    if (check(p, TOK_IDENT)) consume(p); /* skip identifier in VLA context */
                    expect(p, TOK_RBRACKET);
                    cty = type_array(cty, sz);
                } else {
                    break;
                }
            }
            if (check(p, TOK_RPAREN)) {
                consume(p);
                /* Compound literal: (struct S){1,2} or (int[3]){1,2,3} */
                if (check(p, TOK_LBRACE)) {
                    ASTNode *init = parse_initializer_list(p, cty, t.line, t.col);
                    ASTNode *n = ast_new(AST_COMPOUND_LIT, t.line, t.col);
                    n->type = cty;
                    n->as.unary.expr = init;
                    return n;
                }
                ASTNode *operand = parse_expr_with_bp(p, 14);
                ASTNode *n = ast_new(AST_CAST, t.line, t.col);
                n->as.unary.expr = operand;
                n->as.unary.op = TOK_INT; /* store target type as op */
                n->type = cty;
                return n;
            }
            /* No closing paren - abstract declarator broke (nested cast).
             * The rest is the expression operand. */
            ASTNode *operand = parse_expr_with_bp(p, 14);
            ASTNode *n = ast_new(AST_CAST, t.line, t.col);
            n->as.unary.expr = operand;
            n->as.unary.op = TOK_INT;
            n->type = cty;
            return n;
        }
        ASTNode *e = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }
    if (t.kind == TOK_SIZEOF) {
        consume(p);
        if (check(p, TOK_LPAREN)) {
            consume(p); /* consume '(' */
            /* Check if next token is a type keyword or typedef name */
            TokenKind nk = peek(p).kind;
            if (nk == TOK_INT || nk == TOK_CHAR || nk == TOK_VOID ||
                nk == TOK_LONG || nk == TOK_SHORT ||
                nk == TOK_UNSIGNED || nk == TOK_SIGNED ||
                nk == TOK_ENUM || nk == TOK_STRUCT || nk == TOK_UNION || nk == TOK_DOUBLE || nk == TOK_FLOAT ||
                (nk == TOK_IDENT && typedef_lookup(peek(p).val.str_val))) {
                /* sizeof(type) */
                Type *ty = parse_type(p);
                expect(p, TOK_RPAREN);
                ASTNode *n = ast_int(0, t.line, t.col);
                n->type = type_new(TYPE_LONG, 8); n->type->is_unsigned = true;
                n->as.long_val = type_sizeof(ty);
                return n;
            }
            /* sizeof(expr) - expression in parens */
            ASTNode *expr = parse_expr(p);
            expect(p, TOK_RPAREN);
            ASTNode *n = ast_int(0, t.line, t.col);
            n->type = type_new(TYPE_LONG, 8); n->type->is_unsigned = true;
            n->as.long_val = expr->type ? type_sizeof(expr->type) : 0;
            return n;
        } else {
            /* sizeof expr - parse as unary (binding power 14) */
            ASTNode *expr = parse_expr_with_bp(p, 14);
            ASTNode *n = ast_int(0, t.line, t.col);
            n->type = type_new(TYPE_LONG, 8); n->type->is_unsigned = true;
            n->as.long_val = expr->type ? type_sizeof(expr->type) : 0;
            return n;
        }
    }
    parser_error(p, "expected expression");
    return ast_int(0, t.line, t.col);
}

static ASTNode *parse_expr_with_bp(Parser *p, int min_bp) {
    ASTNode *left;

    /* Prefix */
    int ubp = prefix_bp(peek(p).kind);
    if (ubp > 0) {
        Token t = consume(p);
        ASTNode *operand = parse_expr_with_bp(p, ubp);
        left = ast_unary(t.kind, operand, false, t.line, t.col);
        /* Determine unary AST kind */
        switch (t.kind) {
            case TOK_MINUS: left->kind = AST_UNARY; break;
            case TOK_BANG: left->kind = AST_UNARY; break;
            case TOK_TILDE: left->kind = AST_UNARY; break;
            case TOK_STAR: left->kind = AST_DEREF; break;
            case TOK_AMP: left->kind = AST_ADDR; break;
            case TOK_INC: left->kind = AST_PREINC; break;
            case TOK_DEC: left->kind = AST_PREDEC; break;
            case TOK_SIZEOF: break; /* handled in primary */
            default: break;
        }
        left->as.unary.op = t.kind;
        left->as.unary.expr = operand;
        set_binary_type(left);
    } else {
        left = parse_primary(p);
    }

    /* Handle postfix operations: array index and member access */
    for (;;) {
        if (check(p, TOK_LBRACKET)) {
            consume(p);
            ASTNode *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            left = ast_index(left, idx, left->line, left->col);
            set_binary_type(left);
            continue;
        }
        if (peek(p).kind == TOK_DOT) {
            consume(p);
            Token field = consume(p);
            if (field.kind == TOK_IDENT) {
                ASTNode *n = ast_new(AST_MEMBER, left->line, left->col);
                n->as.member.obj = left;
                Type *sty = left->type;
                if (sty && (sty->kind == TYPE_STRUCT || sty->kind == TYPE_UNION)) {
                    for (Member *m = sty->members; m; m = m->next) {
                        if (strcmp(m->name, field.val.str_val) == 0) {
                            n->as.member.member = m;
                            n->type = m->type;
                            break;
                        }
                    }
                }
                left = n;
            }
            continue;
        }
        if (check(p, TOK_ARROW)) {
            consume(p);
            Token field = consume(p);
            if (field.kind == TOK_IDENT) {
                Type *sty = NULL;
                if (left->type && left->type->kind == TYPE_PTR)
                    sty = left->type->base;
                ASTNode *n = ast_new(AST_MEMBER, left->line, left->col);
                n->as.member.obj = left;
                if (sty && (sty->kind == TYPE_STRUCT || sty->kind == TYPE_UNION)) {
                    for (Member *m = sty->members; m; m = m->next) {
                        if (strcmp(m->name, field.val.str_val) == 0) {
                            n->as.member.member = m;
                            n->type = m->type;
                            break;
                        }
                    }
                }
                left = n;
            }
            continue;
        }
        /* Postfix inc/dec */
        if (check(p, TOK_INC)) {
            consume(p);
            left = ast_unary(TOK_INC, left, true, left->line, left->col);
            left->kind = AST_POSTINC;
            continue;
        }
        if (check(p, TOK_DEC)) {
            consume(p);
            left = ast_unary(TOK_DEC, left, true, left->line, left->col);
            left->kind = AST_POSTDEC;
            continue;
        }
        break;
    }

    /* Infix */
    for (;;) {
        TokenKind k = peek(p).kind;
        int bp = infix_bp_left(k);
        if (bp == 0 || bp < min_bp) break;

        if (k == TOK_QUESTION) {
            consume(p);
            ASTNode *cond = left;
            ASTNode *middle = parse_expr(p);
            expect(p, TOK_COLON);
            ASTNode *right = parse_expr_with_bp(p, 2);
            left = ast_new(AST_TERNARY, cond->line, cond->col);
            left->as.if_.cond = cond;
            left->as.if_.then_body = middle;
            left->as.if_.else_body = right;
            set_binary_type(left);
            continue;
        }

        if (k == TOK_ASSIGN || k == TOK_PLUS_EQ || k == TOK_MINUS_EQ ||
            k == TOK_STAR_EQ || k == TOK_SLASH_EQ || k == TOK_PERCENT_EQ ||
            k == TOK_AMP_EQ || k == TOK_PIPE_EQ || k == TOK_CARET_EQ ||
            k == TOK_SHL_EQ || k == TOK_SHR_EQ) {
            Token t = consume(p);
            ASTNode *right = parse_expr_with_bp(p, 2);
            left = ast_assign(left, t.kind, right, t.line, t.col);
            set_binary_type(left);
            continue;
        }

        Token t = consume(p);
        ASTNode *right = parse_expr_with_bp(p, bp + 1);
        left = ast_binary(t.kind, left, right, t.line, t.col);
        set_binary_type(left);
    }

    return left;
}

static ASTNode *parse_expr(Parser *p) {
    return parse_expr_with_bp(p, 0);
}

static ASTNode *parse_block(Parser *p) {
    int sl = peek(p).line, sc = peek(p).col;
    expect(p, TOK_LBRACE);
    ASTNode **stmts = NULL;
    int count = 0, cap = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (count >= cap) {
            cap = cap ? cap * 2 : 16;
            stmts = realloc(stmts, sizeof(ASTNode*) * cap);
        }
        stmts[count++] = parse_block_item(p);
    }
    expect(p, TOK_RBRACE);
    return ast_block(stmts, count, sl, sc);
}

/* Block item = statement or declaration */
static ASTNode *parse_block_item(Parser *p) {
    Token t = peek(p);
    if (t.kind == TOK_INT || t.kind == TOK_CHAR || t.kind == TOK_VOID ||
        t.kind == TOK_LONG || t.kind == TOK_SHORT ||
        t.kind == TOK_UNSIGNED || t.kind == TOK_SIGNED ||
        t.kind == TOK_ENUM || t.kind == TOK_DOUBLE || t.kind == TOK_FLOAT ||
        t.kind == TOK_STRUCT || t.kind == TOK_UNION ||
        t.kind == TOK_STATIC || t.kind == TOK_EXTERN || t.kind == TOK_TYPEDEF || t.kind == TOK_CONST || t.kind == TOK_VOLATILE ||
        (t.kind == TOK_IDENT && typedef_lookup(t.val.str_val) != NULL)) {
        return parse_decl(p, p->locals ? p->locals : p->globals);
    }
    return parse_stmt(p);
}

/* Statement - does NOT accept declarations as statement bodies */
static ASTNode *parse_stmt(Parser *p) {
    Token t = peek(p);

    if (t.kind == TOK_SEMICOLON) {
        consume(p);
        return ast_new(AST_NULL_STMT, t.line, t.col);
    }

    if (t.kind == TOK_LBRACE) return parse_block(p);

    if (t.kind == TOK_RETURN) {
        consume(p);
        ASTNode *expr = NULL;
        if (!check(p, TOK_SEMICOLON)) expr = parse_expr(p);
        expect(p, TOK_SEMICOLON);
        return ast_return(expr, t.line, t.col);
    }

    if (t.kind == TOK_IF) {
        consume(p);
        expect(p, TOK_LPAREN);
        ASTNode *cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        ASTNode *then = parse_stmt(p);
        ASTNode *else_ = NULL;
        if (check(p, TOK_ELSE)) {
            consume(p);
            else_ = parse_stmt(p);
        }
        return ast_if(cond, then, else_, t.line, t.col);
    }

    if (t.kind == TOK_WHILE) {
        consume(p);
        expect(p, TOK_LPAREN);
        ASTNode *cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        ASTNode *n = ast_new(AST_WHILE, t.line, t.col);
        n->as.while_.cond = cond;
        n->as.while_.brk_label = new_unique_name();
        n->as.while_.cont_label = new_unique_name();

        char *prev_brk = brk_label;
        char *prev_cont = cont_label;
        brk_label = n->as.while_.brk_label;
        cont_label = n->as.while_.cont_label;
        n->as.while_.body = parse_stmt(p);
        brk_label = prev_brk;
        cont_label = prev_cont;
        return n;
    }

    if (t.kind == TOK_FOR) {
        consume(p);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(AST_FOR, t.line, t.col);
        n->as.for_.brk_label = new_unique_name();
        n->as.for_.cont_label = new_unique_name();

        char *prev_brk = brk_label;
        char *prev_cont = cont_label;
        brk_label = n->as.for_.brk_label;
        cont_label = n->as.for_.cont_label;

        /* Init: can be empty, declaration, or expression */
        ASTNode *init = NULL;
        if (!check(p, TOK_SEMICOLON)) {
            Token ft = peek(p);
            if (ft.kind == TOK_INT || ft.kind == TOK_CHAR || ft.kind == TOK_VOID ||
                ft.kind == TOK_LONG || ft.kind == TOK_SHORT ||
                ft.kind == TOK_UNSIGNED || ft.kind == TOK_SIGNED ||
                ft.kind == TOK_ENUM || ft.kind == TOK_DOUBLE || ft.kind == TOK_FLOAT ||
                ft.kind == TOK_STRUCT || ft.kind == TOK_UNION ||
                ft.kind == TOK_STATIC || ft.kind == TOK_EXTERN || ft.kind == TOK_TYPEDEF ||
                (ft.kind == TOK_IDENT && typedef_lookup(ft.val.str_val) != NULL)) {
                init = parse_decl(p, p->locals ? p->locals : p->globals);
            } else {
                init = parse_expr(p);
                expect(p, TOK_SEMICOLON);
            }
        } else {
            expect(p, TOK_SEMICOLON);
        }
        ASTNode *cond = check(p, TOK_SEMICOLON) ? NULL : parse_expr(p);
        expect(p, TOK_SEMICOLON);
        ASTNode *inc = check(p, TOK_RPAREN) ? NULL : parse_expr(p);
        expect(p, TOK_RPAREN);
        n->as.for_.init = init;
        n->as.for_.cond = cond;
        n->as.for_.inc = inc;
        n->as.for_.body = parse_stmt(p);

        brk_label = prev_brk;
        cont_label = prev_cont;
        return n;
    }

    if (t.kind == TOK_DO) {
        consume(p);
        ASTNode *n = ast_new(AST_DO_WHILE, t.line, t.col);
        n->as.do_while.brk_label = new_unique_name();
        n->as.do_while.cont_label = new_unique_name();
        n->as.do_while.loop_top_label = new_unique_name();

        char *prev_brk = brk_label;
        char *prev_cont = cont_label;
        brk_label = n->as.do_while.brk_label;
        cont_label = n->as.do_while.cont_label;

        n->as.do_while.body = parse_stmt(p);
        expect(p, TOK_WHILE);
        expect(p, TOK_LPAREN);
        n->as.do_while.cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMICOLON);

        brk_label = prev_brk;
        cont_label = prev_cont;
        return n;
    }

    if (t.kind == TOK_BREAK) {
        consume(p);
        expect(p, TOK_SEMICOLON);
        ASTNode *n = ast_new(AST_BREAK, t.line, t.col);
        n->as.jump_label.label = brk_label ? strdup(brk_label) : NULL;
        return n;
    }

    if (t.kind == TOK_CONTINUE) {
        consume(p);
        expect(p, TOK_SEMICOLON);
        ASTNode *n = ast_new(AST_CONTINUE, t.line, t.col);
        n->as.jump_label.label = cont_label ? strdup(cont_label) : NULL;
        return n;
    }

    if (t.kind == TOK_GOTO) {
        consume(p);
        Token label = expect(p, TOK_IDENT);
        expect(p, TOK_SEMICOLON);
        ASTNode *n = ast_new(AST_GOTO, t.line, t.col);
        n->as.goto_.target = strdup(label.val.str_val);
        return n;
    }

    if (t.kind == TOK_SWITCH) {
        consume(p);
        expect(p, TOK_LPAREN);
        ASTNode *expr = parse_expr(p);
        expect(p, TOK_RPAREN);
        ASTNode *n = ast_new(AST_SWITCH, t.line, t.col);
        n->as.switch_.expr = expr;
        n->as.switch_.case_next = NULL;
        n->as.switch_.default_case = NULL;
        n->as.switch_.brk_label = new_unique_name();

        /* Save and set context for nested case/default parsing */
        ASTNode *prev_switch = current_switch;
        char *prev_brk = brk_label;
        current_switch = n;
        brk_label = n->as.switch_.brk_label;

        n->as.switch_.body = parse_stmt(p);

        current_switch = prev_switch;
        brk_label = prev_brk;
        return n;
    }

    if (t.kind == TOK_CASE) {
        if (!current_switch)
            parser_error(p, "stray case");
        consume(p);
        ASTNode *val = parse_expr(p);
        /* Support GNU case ranges: case start ... end: */
        long begin = 0, end = 0;
        if (val && val->kind == AST_INT_LIT)
            begin = val->as.int_val;
        if (check(p, TOK_ELLIPSIS)) {
            consume(p);
            ASTNode *end_val = parse_expr(p);
            if (end_val && end_val->kind == AST_INT_LIT)
                end = end_val->as.int_val;
        } else {
            end = begin;
        }
        expect(p, TOK_COLON);
        ASTNode *body = parse_stmt(p);
        ASTNode *n = ast_new(AST_CASE, t.line, t.col);
        n->as.case_.val = val;
        n->as.case_.body = body;
        n->as.case_.label = new_unique_name();
        n->as.case_.begin = begin;
        n->as.case_.end = end;
        /* Link into current switch's case list */
        n->as.case_.case_next = current_switch->as.switch_.case_next;
        current_switch->as.switch_.case_next = n;
        return n;
    }

    if (t.kind == TOK_DEFAULT) {
        if (!current_switch)
            parser_error(p, "stray default");
        consume(p);
        expect(p, TOK_COLON);
        ASTNode *body = parse_stmt(p);
        ASTNode *n = ast_new(AST_CASE, t.line, t.col);
        n->as.case_.val = NULL;
        n->as.case_.body = body;
        n->as.case_.label = new_unique_name();
        n->as.case_.begin = 0;
        n->as.case_.end = 0;
        current_switch->as.switch_.default_case = n;
        return n;
    }

    /* Label: identifier followed by colon */
    if (t.kind == TOK_IDENT) {
        Token next = peek_next(p);
        if (next.kind == TOK_COLON) {
            consume(p); /* consume identifier */
            consume(p); /* consume colon */
            ASTNode *body = parse_stmt(p);
            ASTNode *n = ast_new(AST_LABEL, t.line, t.col);
            n->as.label.name = strdup(t.val.str_val);
            n->as.label.stmt = body;
            return n;
        }
    }

    /* Inline assembly: asm("hex bytes"); */
    if (t.kind == TOK_ASM) {
        consume(p); /* consume 'asm' */
        expect(p, TOK_LPAREN);
        Token str = expect(p, TOK_STRING_LIT);
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMICOLON);
        ASTNode *n = ast_new(AST_ASM, t.line, t.col);
        n->as.unary.op = 0;
        n->as.unary.expr = ast_string(str.val.str_val, str.line, str.col);
        return n;
    }

    /* Expression statement */
    ASTNode *expr = parse_expr(p);
    expect(p, TOK_SEMICOLON);
    ASTNode *stmt = ast_new(AST_EXPR_STMT, t.line, t.col);
    /* We'll just return expr and handle in codegen */
    return expr;
}

/* Recursive initializer list parser — handles { expr1, { expr2, ... }, ... } */
static ASTNode *parse_initializer_list(Parser *p, Type *ty, int sl, int sc) {
    consume(p); /* consume '{' */
    ASTNode **items = NULL;
    int count = 0, cap = 0;
    while (!check(p, TOK_RBRACE)) {
        if (count >= cap) { cap = cap ? cap * 2 : 16; items = realloc(items, sizeof(ASTNode*) * cap); }
        /* Handle designated initializer: [index] = value */
        if (check(p, TOK_LBRACKET)) {
            consume(p); /* '[' */
            ASTNode *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            expect(p, TOK_ASSIGN);
            ASTNode *val;
            if (check(p, TOK_LBRACE))
                val = parse_initializer_list(p, ty, sl, sc);
            else
                val = parse_expr_with_bp(p, 2);
            /* Mark as designated: store sentinel (-1) then idx then val */
            ASTNode *sentinel = ast_int(-1, sl, sc);
            items[count++] = sentinel;
            items[count++] = idx;
            items[count++] = val;
        } else if (check(p, TOK_LBRACE))
            items[count++] = parse_initializer_list(p, ty, sl, sc);
        else
            items[count++] = parse_expr_with_bp(p, 2);
        if (check(p, TOK_COMMA)) consume(p);
    }
    expect(p, TOK_RBRACE);
    return ast_block(items, count, sl, sc);
}

static ASTNode *parse_decl(Parser *p, SymTable *st) {
    int sl = peek(p).line, sc = peek(p).col;

    /* Consume storage class specifiers */
    bool is_static = false, is_extern = false, is_typedef = false;
    while (true) {
        TokenKind k = peek(p).kind;
        if (k == TOK_STATIC && !is_static) { consume(p); is_static = true; }
        else if (k == TOK_EXTERN && !is_extern) { consume(p); is_extern = true; }
        else if (k == TOK_TYPEDEF && !is_typedef) { consume(p); is_typedef = true; }
        else break;
    }
    Type *ty = parse_type(p);

    /* Also check for storage class specifiers after type */
    while (true) {
        TokenKind k = peek(p).kind;
        if (k == TOK_STATIC && !is_static) { consume(p); is_static = true; }
        else if (k == TOK_EXTERN && !is_extern) { consume(p); is_extern = true; }
        else if (k == TOK_TYPEDEF && !is_typedef) { consume(p); is_typedef = true; }
        else break;
    }

    /* After storage class, check for more type specifiers (e.g., int static long a;) */
    if (peek(p).kind == TOK_INT || peek(p).kind == TOK_LONG || peek(p).kind == TOK_SHORT ||
        peek(p).kind == TOK_CHAR || peek(p).kind == TOK_VOID ||
        peek(p).kind == TOK_UNSIGNED || peek(p).kind == TOK_SIGNED) {
        Type *ty2 = parse_type(p);
        ty = common_type(ty, ty2);
    }

    /* Parse the name (identifier for both functions and variables).
     * If we see '(', it might be a complex declarator like int (*f)(). */
    char *vname = NULL;
    if (check(p, TOK_LPAREN)) {
        /* Complex declarator: ( *name ) or ( *name[...] ) or ( name(...) ) */
        Token next = peek_next(p);
        if (next.kind == TOK_STAR || next.kind == TOK_IDENT || next.kind == TOK_RPAREN) {
            consume(p); /* consume '(' */
            /* Save position, parse inner declarator */
            /* Handle pointer stars first */
            Type *inner_ty = ty;
            while (check(p, TOK_STAR)) {
                consume(p);
                inner_ty = type_ptr(inner_ty);
            }
            /* Read name */
            if (check(p, TOK_IDENT)) {
                vname = consume(p).val.str_val;
            }
            /* Inner array brackets: (*name)[size] */
            while (vname && check(p, TOK_LBRACKET)) {
                consume(p);
                int arr_size = 0;
                if (check(p, TOK_INT_LIT)) arr_size = consume(p).val.int_val;
                expect(p, TOK_RBRACKET);
                inner_ty = type_array(inner_ty, arr_size);
            }
            /* Inner function params: (*name)(params) */
            if (vname && check(p, TOK_LPAREN)) {
                consume(p);
                ASTNode **params = NULL;
                int param_count = 0, param_cap = 0;
                Type **param_types = NULL;
                if (!check(p, TOK_RPAREN)) {
                    do {
                        Type *pty = parse_type(p);
                        char *pname = NULL;
                        if (check(p, TOK_IDENT)) pname = consume(p).val.str_val;
                        if (pty->kind == TYPE_VOID && !pname && check(p, TOK_RPAREN)) break;
                        if (param_count >= param_cap) {
                            param_cap = param_cap ? param_cap * 2 : 8;
                            params = realloc(params, sizeof(ASTNode*) * param_cap);
                            param_types = realloc(param_types, sizeof(Type*) * param_cap);
                        }
                        params[param_count] = pname ? ast_var_decl(pname, pty, NULL, 0, 0) : NULL;
                        param_types[param_count] = pty;
                        param_count++;
                    } while (check(p, TOK_COMMA) && (consume(p), 1));
                }
                expect(p, TOK_RPAREN);
                inner_ty = type_func(inner_ty, param_types, param_count, false);
                inner_ty->param_count = param_count;
            }
            expect(p, TOK_RPAREN); /* close '(' from complex declarator */
            ty = inner_ty;
            /* Outer modifiers [size] or (params) apply to the inner base,
             * not the outer pointer. For (*a)[3]: inner_base=int, apply [3]→array[3]of int,
             * then pointer→pointer to array[3]of int */
            while (vname && (check(p, TOK_LBRACKET) || check(p, TOK_LPAREN))) {
                if (check(p, TOK_LBRACKET)) {
                    consume(p);
                    int arr_size = 0;
                    if (check(p, TOK_INT_LIT)) arr_size = consume(p).val.int_val;
                    expect(p, TOK_RBRACKET);
                    /* For (*a)[N]: wrap the pointer's base, not the pointer itself */
                    if (inner_ty->kind == TYPE_PTR) {
                        inner_ty->base = type_array(inner_ty->base, arr_size);
                    } else {
                        ty = type_array(ty, arr_size);
                    }
                } else {
                    consume(p); /* consume '(' */
                    Type **param_types = NULL;
                    int pc = 0, pcap = 0;
                    if (!check(p, TOK_RPAREN)) {
                        do {
                            Type *pty = parse_type(p);
                            char *pname = NULL;
                            if (check(p, TOK_IDENT)) pname = consume(p).val.str_val;
                            if (pty->kind == TYPE_VOID && !pname && check(p, TOK_RPAREN)) break;
                            if (pc >= pcap) { pcap = pcap ? pcap * 2 : 4; param_types = realloc(param_types, sizeof(Type*)*pcap); }
                            param_types[pc++] = pty;
                        } while (check(p, TOK_COMMA) && (consume(p), 1));
                    }
                    expect(p, TOK_RPAREN);
                    if (inner_ty->kind == TYPE_PTR) {
                        inner_ty->base = type_func(inner_ty->base, param_types, pc, false);
                        if (inner_ty->base) inner_ty->base->param_count = pc;
                    } else {
                        ty = type_func(ty, param_types, pc, false);
                        if (ty) ty->param_count = pc;
                    }
                }
            }
        }
    } else if (check(p, TOK_IDENT) && peek(p).val.str_val) {
        vname = consume(p).val.str_val;
    }

    /* Array declarator: name followed by [size] (possibly multi-dimensional) */
    while (vname && check(p, TOK_LBRACKET)) {
        consume(p);
        int arr_size = 0;
        if (check(p, TOK_INT_LIT)) {
            arr_size = consume(p).val.int_val;
        }
        expect(p, TOK_RBRACKET);
        ty = type_array(ty, arr_size);
    }

    /* Typedef: add to typedef table and skip */
    if (is_typedef && vname) {
        typedef_add(vname, ty);
        expect(p, TOK_SEMICOLON);
        return ast_new(AST_NULL_STMT, sl, sc);
    }

    /* Function declaration - check for '(' after name */
    if (vname && check(p, TOK_LPAREN)) {
        consume(p);
        /* Parse params */
        ASTNode **params = NULL;
        int param_count = 0, param_cap = 0;
        Type **param_types = NULL;
        int type_cap = 0;

        if (!check(p, TOK_RPAREN)) {
            do {
                Type *pty = parse_type(p);
                char *pname = NULL;
                if (check(p, TOK_LPAREN)) {
                    /* Complex declarator in param: int (*a)[3] */
                    consume(p);
                    while (check(p, TOK_STAR)) consume(p); /* skip pointers */
                    if (check(p, TOK_IDENT)) pname = consume(p).val.str_val;
                    expect(p, TOK_RPAREN);
                    /* Skip outer modifiers like [3] */
                    while (check(p, TOK_LBRACKET)) {
                        consume(p);
                        if (check(p, TOK_INT_LIT)) consume(p);
                        expect(p, TOK_RBRACKET);
                    }
                } else if (check(p, TOK_IDENT)) {
                    pname = consume(p).val.str_val;
                }
                /* (void) means no parameters in C */
                if (pty->kind == TYPE_VOID && !pname && check(p, TOK_RPAREN)) {
                    break;
                }
                /* Handle array declarator in params: int x[100] → int *x, int x[2][3] → int (*)[3] */
                while (check(p, TOK_LBRACKET)) {
                    consume(p);
                    if (check(p, TOK_INT_LIT)) consume(p); /* skip size */
                    expect(p, TOK_RBRACKET);
                    pty = type_ptr(pty); /* array param decays to pointer */
                }
                if (param_count >= param_cap) {
                    param_cap = param_cap ? param_cap * 2 : 8;
                    params = realloc(params, sizeof(ASTNode*) * param_cap);
                    param_types = realloc(param_types, sizeof(Type*) * param_cap);
                }
                params[param_count] = pname ? ast_var_decl(pname, pty, NULL, 0, 0) : NULL;
                param_types[param_count] = pty;
                param_count++;
            } while (check(p, TOK_COMMA) && (consume(p), 1));
        }
        expect(p, TOK_RPAREN);
        Type *fty = type_func(ty, param_types, param_count, false);
        fty->param_count = param_count;

        /* Prototype */
        if (check(p, TOK_SEMICOLON)) {
            consume(p);
            Symbol *s = symtable_insert(p->globals, vname, fty, SYM_FUNC);
            s->is_defined = false;
            ASTNode *fn = ast_func_decl(vname, fty->base, params, param_count, NULL, sl, sc);
            fn->as.func_decl.is_static = is_static;
            return fn;
        }

        /* Full definition - only allowed at top level */
        if (st != p->globals) {
            parser_error(p, "nested function definitions not allowed");
            return ast_func_decl(vname, fty->base, params, param_count, NULL, sl, sc);
        }
        p->locals = symtable_new(p->globals);
        for (int i = 0; i < param_count; i++) {
            if (params[i]) {
                symtable_insert(p->locals, params[i]->as.var_decl.name,
                                params[i]->as.var_decl.type, SYM_PARAM);
            }
        }
        ASTNode *body = parse_block(p);
        int frame_size = p->locals->frame_size;
        symtable_free(p->locals);
        p->locals = NULL;

        Symbol *s = symtable_insert(p->globals, vname, fty, SYM_FUNC);
        s->is_defined = true;
        s->offset = frame_size;

        ASTNode *fn = ast_func_decl(vname, fty->base, params, param_count, body, sl, sc);
        fn->as.func_decl.is_static = is_static;
        return fn;
    }

    /* Variable declaration */
    if (vname) {
        if (is_extern) {
            /* Extern: declare but don't allocate */
            if (st == p->locals)
                symtable_insert(p->locals, vname, ty, SYM_GLOBAL);
            else
                symtable_insert(p->globals, vname, ty, SYM_GLOBAL);
        } else if (st == p->locals) {
            symtable_insert(p->locals, vname, ty, is_static ? SYM_GLOBAL : SYM_LOCAL);
        } else {
            symtable_insert(p->globals, vname, ty, SYM_GLOBAL);
        }
    }

    /* Recursive initializer list parser */
    ASTNode *init = NULL;
    if (check(p, TOK_ASSIGN)) {
        consume(p);
        if (check(p, TOK_LBRACE)) {
            init = parse_initializer_list(p, ty, sl, sc);
            if (ty && ty->kind == TYPE_ARRAY && ty->size == 0)
                ty->size = (init && init->kind == AST_BLOCK) ? init->as.block.count : 0;
        } else {
            init = parse_expr(p);
            if (ty && ty->kind == TYPE_ARRAY && ty->size == 0 &&
                init && init->kind == AST_STRING_LIT) {
                int slen = strlen(init->as.str_val);
                ty->size = slen + 1;
            }
        }
    }

    /* Handle comma-separated declarations: int a, *b = 0, c[3]; */
    if (check(p, TOK_COMMA)) {
        consume(p);
        /* Save first declaration info */
        ASTNode *first = ast_var_decl(vname ? vname : "?", ty, init, sl, sc);
        first->as.var_decl.is_static = is_static;
        first->as.var_decl.is_extern = is_extern;
        /* Create a block to hold all declarations */
        ASTNode **decls = malloc(sizeof(ASTNode*) * 8);
        int dcount = 0, dcap = 8;
        if (vname) decls[dcount++] = first;
        /* Parse remaining declarators */
        while (true) {
            Type *dty = ty;
            /* Parse pointer stars */
            while (check(p, TOK_STAR)) { consume(p); dty = type_ptr(dty); }
            char *dname = NULL;
            if (check(p, TOK_IDENT)) dname = consume(p).val.str_val;
            /* Array declarator */
            if (dname && check(p, TOK_LBRACKET)) {
                consume(p);
                int sz = 0;
                if (check(p, TOK_INT_LIT)) sz = consume(p).val.int_val;
                expect(p, TOK_RBRACKET);
                dty = type_array(dty, sz);
            }
            ASTNode *dinit = NULL;
            if (check(p, TOK_ASSIGN)) { consume(p); dinit = parse_expr(p); }
            if (dname && dcount < dcap) {
                ASTNode *dv = ast_var_decl(dname, dty, dinit, sl, sc);
                dv->as.var_decl.is_static = is_static;
                dv->as.var_decl.is_extern = is_extern;
                decls[dcount++] = dv;
            }
            if (check(p, TOK_COMMA)) { consume(p); continue; }
            break;
        }
        expect(p, TOK_SEMICOLON);
        if (dcount == 0) { free(decls); return ast_new(AST_NULL_STMT, sl, sc); }
        return ast_block(decls, dcount, sl, sc);
    }

    expect(p, TOK_SEMICOLON);

    if (!vname) return ast_new(AST_NULL_STMT, sl, sc);
    ASTNode *result = ast_var_decl(vname, ty, init, sl, sc);
    result->as.var_decl.is_static = is_static;
    result->as.var_decl.is_extern = is_extern;
    return result;
}

void parser_init(Parser *p, Lexer *lex) {
    memset(p, 0, sizeof(*p));
    p->lex = lex;
    p->globals = symtable_new(NULL);
}

ASTNode *parser_parse(Parser *p) {
    ASTNode **decls = NULL;
    int count = 0, cap = 0;
    while (!check(p, TOK_EOF) && !p->has_error) {
        ASTNode *d = parse_decl(p, p->globals);
        if (d) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                decls = realloc(decls, sizeof(ASTNode*) * cap);
            }
            decls[count++] = d;
        }
    }
    return ast_block(decls, count, 1, 1);
}
