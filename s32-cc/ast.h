#ifndef S32CC_AST_H
#define S32CC_AST_H

#include "lexer.h"
#include <stdint.h>

typedef enum {
    /* Expressions */
    AST_INT_LIT, AST_CHAR_LIT, AST_STRING_LIT, AST_IDENT,
    AST_BINARY, AST_UNARY, AST_ASSIGN, AST_COMMA,
    AST_CALL, AST_INDEX, AST_DEREF, AST_ADDR, AST_SIZEOF,
    AST_CAST, AST_TERNARY, AST_POSTINC, AST_POSTDEC,
    AST_PREINC, AST_PREDEC,
    /* Member access */
    AST_MEMBER,
    /* Statements */
    AST_EXPR_STMT, AST_BLOCK, AST_RETURN, AST_IF, AST_WHILE,
    AST_FOR, AST_DO_WHILE, AST_BREAK, AST_CONTINUE,
    AST_NULL_STMT, AST_VAR_DECL, AST_FUNC_DECL, AST_ASM,
    AST_LABEL, AST_GOTO,
    /* Misc */
    AST_SWITCH, AST_CASE, AST_DEFAULT,
} ASTKind;

typedef enum {
    TYPE_VOID, TYPE_CHAR, TYPE_INT,
    TYPE_LONG, TYPE_ENUM, TYPE_DOUBLE,
    TYPE_PTR, TYPE_ARRAY, TYPE_FUNC,
    TYPE_STRUCT, TYPE_UNION,
} TypeKind;

typedef struct Member {
    char *name;
    struct Type *type;
    int offset;      /* byte offset from struct start */
    struct Member *next;
} Member;

typedef struct Type {
    TypeKind kind;
    int size;       /* sizeof in bytes */
    int align;
    bool is_unsigned;
    struct Type *base;  /* for PTR/ARRAY/FUNC */
    /* FUNC params */
    struct Type **param_types;
    int param_count;
    bool is_vararg;
    /* Struct/union members */
    Member *members;
    bool is_flexible;
} Type;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTKind kind;
    int line, col;
    Type *type;
    union {
        /* Literals */
        int int_val;
        char char_val;
        char *str_val;
        double double_val;
        int64_t long_val;
        char *ident;
        /* Binary: op left right */
        struct { int op; ASTNode *left, *right; } binary;
        /* Unary: op expr */
        struct { int op; ASTNode *expr; bool postfix; } unary;
        /* Assign: lvalue op rvalue */
        struct { ASTNode *lvalue; int op; ASTNode *rvalue; } assign;
        /* Comma: list, count */
        struct { ASTNode **list; int count; } comma;
        /* Call: func args, arg_count */
        struct { ASTNode *func; ASTNode **args; int arg_count; } call;
        /* Index: base index */
        struct { ASTNode *base; ASTNode *index; } index;
        /* Member: struct_obj, member_name */
        struct { ASTNode *obj; Member *member; } member;
        /* Block: stmts, count */
        struct { ASTNode **stmts; int count; } block;
        /* Return */
        struct { ASTNode *expr; } ret;
        /* If */
        struct { ASTNode *cond; ASTNode *then_body; ASTNode *else_body; } if_;
        /* While */
        struct { ASTNode *cond; ASTNode *body; char *brk_label; char *cont_label; } while_;
        /* For */
        struct { ASTNode *init; ASTNode *cond; ASTNode *inc; ASTNode *body; char *brk_label; char *cont_label; } for_;
        /* DoWhile */
        struct { ASTNode *body; ASTNode *cond; char *brk_label; char *cont_label; char *loop_top_label; } do_while;
        /* VarDecl: name, type, init */
        struct { char *name; Type *type; ASTNode *init; bool is_static; bool is_extern; int static_offset; } var_decl;
        /* Label: name, stmt */
        struct { char *name; ASTNode *stmt; } label;
        /* Goto: target label name */
        struct { char *target; } goto_;
        /* Break/Continue: jump target label */
        struct { char *label; } jump_label;
        /* FuncDecl: name, return_type, params, param_count, body */
        struct { char *name; Type *return_type; ASTNode **params; int param_count; ASTNode *body; bool is_static; } func_decl;
        /* Switch */
        struct {
            ASTNode *expr;
            ASTNode *body;
            ASTNode *case_next;    /* linked list of case nodes */
            ASTNode *default_case; /* default case node, or NULL */
            char *brk_label;       /* break target label */
        } switch_;
        /* Case */
        struct {
            ASTNode *val;
            ASTNode *body;
            char *label;           /* unique label for this case */
            ASTNode *case_next;    /* next case in linked list */
            long begin;            /* range start (for GNU case ranges) */
            long end;              /* range end */
        } case_;
    } as;
};

Type *type_new(TypeKind kind, int size);
Type *type_ptr(Type *base);
Type *type_array(Type *base, int len);
Type *type_func(Type *ret, Type **params, int param_count, bool vararg);
Type *type_enum(void);
Type *type_struct(void);
Type *type_union(void);
bool is_integer(Type *t);
bool type_equal(Type *a, Type *b);
int type_sizeof(Type *t);
Member *member_new(char *name, Type *type);

ASTNode *ast_new(ASTKind kind, int line, int col);
ASTNode *ast_int(int val, int line, int col);
ASTNode *ast_char(char val, int line, int col);
ASTNode *ast_string(char *val, int line, int col);
ASTNode *ast_ident(char *name, int line, int col);
ASTNode *ast_binary(int op, ASTNode *left, ASTNode *right, int line, int col);
ASTNode *ast_unary(int op, ASTNode *expr, bool postfix, int line, int col);
ASTNode *ast_assign(ASTNode *lvalue, int op, ASTNode *rvalue, int line, int col);
ASTNode *ast_call(ASTNode *func, ASTNode **args, int arg_count, int line, int col);
ASTNode *ast_index(ASTNode *base, ASTNode *index, int line, int col);
ASTNode *ast_block(ASTNode **stmts, int count, int line, int col);
ASTNode *ast_return(ASTNode *expr, int line, int col);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body, int line, int col);
ASTNode *ast_while(ASTNode *cond, ASTNode *body, int line, int col);
ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *inc, ASTNode *body, int line, int col);
ASTNode *ast_do_while(ASTNode *body, ASTNode *cond, int line, int col);
ASTNode *ast_var_decl(char *name, Type *type, ASTNode *init, int line, int col);
ASTNode *ast_func_decl(char *name, Type *ret, ASTNode **params, int param_count, ASTNode *body, int line, int col);
ASTNode *ast_break(int line, int col);
ASTNode *ast_continue(int line, int col);

#endif
