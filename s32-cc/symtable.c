#include "symtable.h"
#include <stdlib.h>
#include <string.h>

SymTable *symtable_new(SymTable *parent) {
    SymTable *st = calloc(1, sizeof(SymTable));
    st->parent = parent;
    st->next_offset = 1; /* slot 0 is frame pointer */
    return st;
}

Symbol *symtable_insert(SymTable *st, const char *name, Type *type, SymKind kind) {
    Symbol *s = calloc(1, sizeof(Symbol));
    s->name = strdup(name);
    s->type = type;
    s->kind = kind;
    if (kind == SYM_LOCAL || kind == SYM_PARAM) {
        s->offset = st->next_offset++;
        st->frame_size = st->next_offset;
    }
    s->next = st->entries;
    st->entries = s;
    return s;
}

Symbol *symtable_lookup(SymTable *st, const char *name) {
    for (SymTable *t = st; t; t = t->parent) {
        for (Symbol *s = t->entries; s; s = s->next) {
            if (strcmp(s->name, name) == 0) return s;
        }
    }
    return NULL;
}

Symbol *symtable_lookup_local(SymTable *st, const char *name) {
    for (Symbol *s = st->entries; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

void symtable_free(SymTable *st) {
    if (!st) return;
    Symbol *s = st->entries;
    while (s) {
        Symbol *next = s->next;
        free(s->name);
        free(s);
        s = next;
    }
    free(st);
}
