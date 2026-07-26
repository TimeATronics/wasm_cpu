#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Type *type_new(TypeKind kind, int size) {
    Type *t = calloc(1, sizeof(Type));
    t->kind = kind;
    t->size = size;
    t->align = size;
    return t;
}

Type *type_ptr(Type *base) {
    Type *t = type_new(TYPE_PTR, 4);
    t->base = base;
    t->is_unsigned = true;
    return t;
}

Type *type_array(Type *base, int len) {
    Type *t = type_new(TYPE_ARRAY, base->size * len);
    t->base = base;
    t->size = len;
    return t;
}

Type *type_func(Type *ret, Type **params, int param_count, bool vararg) {
    Type *t = type_new(TYPE_FUNC, 4);
    t->base = ret;
    t->param_types = params;
    t->param_count = param_count;
    t->is_vararg = vararg;
    return t;
}

Type *type_enum(void) {
    Type *t = type_new(TYPE_ENUM, 4);
    t->align = 4;
    return t;
}

Type *type_struct(void) {
    Type *t = type_new(TYPE_STRUCT, 0);
    t->align = 1;
    return t;
}

Type *type_union(void) {
    Type *t = type_new(TYPE_UNION, 0);
    t->align = 1;
    return t;
}

Member *member_new(char *name, Type *type) {
    Member *m = calloc(1, sizeof(Member));
    m->name = name;
    m->type = type;
    return m;
}

bool is_integer(Type *t) {
    if (!t) return false;
    return t->kind == TYPE_CHAR || t->kind == TYPE_INT ||
           t->kind == TYPE_LONG || t->kind == TYPE_ENUM;
}

bool type_equal(Type *a, Type *b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->is_unsigned != b->is_unsigned) return false;
    if (a->kind == TYPE_STRUCT || a->kind == TYPE_UNION)
        return a == b;
    if (a->kind == TYPE_PTR || a->kind == TYPE_ARRAY)
        return type_equal(a->base, b->base);
    if (a->kind == TYPE_FUNC) {
        if (!type_equal(a->base, b->base)) return false;
        if (a->param_count != b->param_count) return false;
        for (int i = 0; i < a->param_count; i++)
            if (!type_equal(a->param_types[i], b->param_types[i])) return false;
        return true;
    }
    return true;
}

int type_sizeof(Type *t) {
    if (!t) return 0;
    if (t->kind == TYPE_ARRAY) return t->base->size * t->size;
    if (t->kind == TYPE_FUNC) return 4;
    if (t->kind == TYPE_VOID) return 1;
    if (t->kind == TYPE_DOUBLE) return 8;
    if (t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) return t->size;
    return t->size;
}

ASTNode *ast_new(ASTKind kind, int line, int col) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->kind = kind;
    n->line = line;
    n->col = col;
    return n;
}

ASTNode *ast_int(int val, int line, int col) {
    ASTNode *n = ast_new(AST_INT_LIT, line, col);
    n->as.int_val = val;
    n->type = type_new(TYPE_INT, 4);
    return n;
}

ASTNode *ast_char(char val, int line, int col) {
    ASTNode *n = ast_new(AST_CHAR_LIT, line, col);
    n->as.char_val = val;
    n->type = type_new(TYPE_INT, 4); /* char constants are int in C */
    return n;
}

ASTNode *ast_string(char *val, int line, int col) {
    ASTNode *n = ast_new(AST_STRING_LIT, line, col);
    n->as.str_val = val;
    n->type = type_ptr(type_new(TYPE_CHAR, 1));
    return n;
}

ASTNode *ast_ident(char *name, int line, int col) {
    ASTNode *n = ast_new(AST_IDENT, line, col);
    n->as.ident = name;
    return n;
}

ASTNode *ast_binary(int op, ASTNode *left, ASTNode *right, int line, int col) {
    ASTNode *n = ast_new(AST_BINARY, line, col);
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

ASTNode *ast_unary(int op, ASTNode *expr, bool postfix, int line, int col) {
    ASTNode *n = ast_new(postfix ? (op == TOK_INC ? AST_POSTINC : AST_POSTDEC) :
                         (op == TOK_INC ? AST_PREINC : AST_PREDEC), line, col);
    n->as.unary.op = op;
    n->as.unary.expr = expr;
    n->as.unary.postfix = postfix;
    return n;
}

ASTNode *ast_assign(ASTNode *lvalue, int op, ASTNode *rvalue, int line, int col) {
    ASTNode *n = ast_new(AST_ASSIGN, line, col);
    n->as.assign.lvalue = lvalue;
    n->as.assign.op = op;
    n->as.assign.rvalue = rvalue;
    return n;
}

ASTNode *ast_call(ASTNode *func, ASTNode **args, int arg_count, int line, int col) {
    ASTNode *n = ast_new(AST_CALL, line, col);
    n->as.call.func = func;
    n->as.call.args = args;
    n->as.call.arg_count = arg_count;
    return n;
}

ASTNode *ast_index(ASTNode *base, ASTNode *index, int line, int col) {
    ASTNode *n = ast_new(AST_INDEX, line, col);
    n->as.index.base = base;
    n->as.index.index = index;
    return n;
}

ASTNode *ast_block(ASTNode **stmts, int count, int line, int col) {
    ASTNode *n = ast_new(AST_BLOCK, line, col);
    n->as.block.stmts = stmts;
    n->as.block.count = count;
    return n;
}

ASTNode *ast_return(ASTNode *expr, int line, int col) {
    ASTNode *n = ast_new(AST_RETURN, line, col);
    n->as.ret.expr = expr;
    return n;
}

ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body, int line, int col) {
    ASTNode *n = ast_new(AST_IF, line, col);
    n->as.if_.cond = cond;
    n->as.if_.then_body = then_body;
    n->as.if_.else_body = else_body;
    return n;
}

ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(AST_WHILE, line, col);
    n->as.while_.cond = cond;
    n->as.while_.body = body;
    return n;
}

ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *inc, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(AST_FOR, line, col);
    n->as.for_.init = init;
    n->as.for_.cond = cond;
    n->as.for_.inc = inc;
    n->as.for_.body = body;
    return n;
}

ASTNode *ast_do_while(ASTNode *body, ASTNode *cond, int line, int col) {
    ASTNode *n = ast_new(AST_DO_WHILE, line, col);
    n->as.do_while.body = body;
    n->as.do_while.cond = cond;
    return n;
}

ASTNode *ast_var_decl(char *name, Type *type, ASTNode *init, int line, int col) {
    ASTNode *n = ast_new(AST_VAR_DECL, line, col);
    n->as.var_decl.name = name;
    n->as.var_decl.type = type;
    n->as.var_decl.init = init;
    n->as.var_decl.is_static = false;
    n->as.var_decl.is_extern = false;
    n->as.var_decl.static_offset = -1;
    return n;
}

ASTNode *ast_func_decl(char *name, Type *ret, ASTNode **params, int param_count, ASTNode *body, int line, int col) {
    ASTNode *n = ast_new(AST_FUNC_DECL, line, col);
    n->as.func_decl.name = name;
    n->as.func_decl.return_type = ret;
    n->as.func_decl.params = params;
    n->as.func_decl.param_count = param_count;
    n->as.func_decl.body = body;
    n->as.func_decl.is_static = false;
    return n;
}

ASTNode *ast_break(int line, int col) {
    return ast_new(AST_BREAK, line, col);
}

ASTNode *ast_continue(int line, int col) {
    return ast_new(AST_CONTINUE, line, col);
}
