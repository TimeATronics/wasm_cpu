#ifndef S32CC_SYMTABLE_H
#define S32CC_SYMTABLE_H

#include "ast.h"

typedef enum {
    SYM_LOCAL, SYM_GLOBAL, SYM_PARAM, SYM_FUNC,
} SymKind;

typedef struct Symbol {
    char *name;
    Type *type;
    SymKind kind;
    int offset;     /* stack offset for locals/params, or label addr for globals */
    bool is_defined; /* has body */
    struct Symbol *next;
} Symbol;

typedef struct SymTable {
    Symbol *entries;
    struct SymTable *parent;
    int frame_size;  /* current stack frame size in words */
    int next_offset; /* next available stack offset */
} SymTable;

SymTable *symtable_new(SymTable *parent);
Symbol *symtable_insert(SymTable *st, const char *name, Type *type, SymKind kind);
Symbol *symtable_lookup(SymTable *st, const char *name);
Symbol *symtable_lookup_local(SymTable *st, const char *name);
void symtable_free(SymTable *st);

#endif
