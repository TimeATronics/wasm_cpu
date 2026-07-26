#include "codegen.h"
#include "../shared/elf32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* helpers */

void codegen_init(Codegen *cg) {
    memset(cg, 0, sizeof(*cg));
    cg->code_cap = 65536;
    cg->code = malloc(cg->code_cap);
    cg->label_cap = 256;
    cg->labels = malloc(sizeof(*cg->labels) * cg->label_cap);
    cg->fixup_cap = 256;
    cg->fixups = malloc(sizeof(*cg->fixups) * cg->fixup_cap);
    cg->string_cap = 64;
    cg->strings = malloc(sizeof(*cg->strings) * cg->string_cap);
    cg->data_end = 0;
    cg->next_var_addr = 1; /* slot 0 reserved for frame pointer */
}

static void emit(Codegen *cg, uint8_t byte) {
    if (cg->code_len >= cg->code_cap) {
        cg->code_cap *= 2;
        cg->code = realloc(cg->code, cg->code_cap);
    }
    cg->code[cg->code_len++] = byte;
}

static void emit_u32(Codegen *cg, uint32_t val) {
    emit(cg, val & 0xFF);
    emit(cg, (val >> 8) & 0xFF);
    emit(cg, (val >> 16) & 0xFF);
    emit(cg, (val >> 24) & 0xFF);
}

static void emit_op(Codegen *cg, int op) { emit(cg, (uint8_t)op); }

static void emit_push(Codegen *cg, uint32_t val) {
    emit_op(cg, OP_PUSH);
    emit_u32(cg, val);
}

static void emit_local_set(Codegen *cg, int addr) {
    emit_op(cg, OP_LOCAL_SET);
    emit(cg, (uint8_t)addr);
}

static void emit_local_get(Codegen *cg, int addr) {
    emit_op(cg, OP_LOCAL_GET);
    emit(cg, (uint8_t)addr);
}

static void emit_push_frame_size(Codegen *cg) {
    emit_push(cg, (uint32_t)cg->next_var_addr);
    emit_op(cg, OP_GT_R);
}

static int add_label(Codegen *cg, const char *name) {
    int addr = cg->code_len;
    if (cg->label_count >= cg->label_cap) {
        cg->label_cap *= 2;
        cg->labels = realloc(cg->labels, sizeof(*cg->labels) * cg->label_cap);
    }
    cg->labels[cg->label_count].name = strdup(name);
    cg->labels[cg->label_count].addr = addr;
    return cg->label_count++;
}

static int find_label(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->label_count; i++)
        if (strcmp(cg->labels[i].name, name) == 0)
            return cg->labels[i].addr;
    return -1;
}

static void add_fixup(Codegen *cg, int code_pos, const char *label) {
    if (cg->fixup_count >= cg->fixup_cap) {
        cg->fixup_cap *= 2;
        cg->fixups = realloc(cg->fixups, sizeof(*cg->fixups) * cg->fixup_cap);
    }
    cg->fixups[cg->fixup_count].code_pos = code_pos;
    cg->fixups[cg->fixup_count].label = strdup(label);
    cg->fixup_count++;
}

static int add_string(Codegen *cg, const char *str, int len) {
    if (cg->string_count >= cg->string_cap) {
        cg->string_cap *= 2;
        cg->strings = realloc(cg->strings, sizeof(*cg->strings) * cg->string_cap);
    }
    int addr = cg->data_end;
    cg->strings[cg->string_count].str = malloc(len + 1);
    memcpy(cg->strings[cg->string_count].str, str, len);
    cg->strings[cg->string_count].str[len] = 0;
    cg->strings[cg->string_count].addr = addr;
    cg->strings[cg->string_count].len = len;
    cg->data_end += len + 1; /* +1 for null terminator */
    cg->string_count++;
    return addr;
}

/* Patch a GOTO/BR_IF target at code_pos */
static void patch_jump(Codegen *cg, int code_pos, int target) {
    cg->code[code_pos + 1] = (target)       & 0xFF;
    cg->code[code_pos + 2] = (target >> 8)  & 0xFF;
    cg->code[code_pos + 3] = (target >> 16) & 0xFF;
    cg->code[code_pos + 4] = (target >> 24) & 0xFF;
}

/* Resolve all forward-referenced fixups */
static void resolve_fixups(Codegen *cg) {
    for (int i = 0; i < cg->fixup_count; i++) {
        int addr = find_label(cg, cg->fixups[i].label);
        if (addr < 0) {
            fprintf(stderr, "s32-cc: undefined label '%s'\n", cg->fixups[i].label);
            continue;
        }
        patch_jump(cg, cg->fixups[i].code_pos, addr);
    }
}

/* Constant folding helpers */

static bool is_const_int(ASTNode *n) {
    return n && n->kind == AST_INT_LIT && 
           n->type && n->type->kind != TYPE_DOUBLE;
}

static int get_const_int(ASTNode *n) {
    return n->as.int_val;
}

/* Try to fold a binary operation at compile time.
 * Returns true if folding succeeded and the result was emitted. */
static bool fold_const_binary(Codegen *cg, int op, ASTNode *left, ASTNode *right) {
    if (!is_const_int(left) || !is_const_int(right))
        return false;
    
    int a = get_const_int(left);
    int b = get_const_int(right);
    int result = 0;
    bool can_fold = true;
    
    switch (op) {
        case TOK_PLUS:    result = a + b; break;
        case TOK_MINUS:   result = a - b; break;
        case TOK_STAR:    result = a * b; break;
        case TOK_SLASH:   if (b == 0) can_fold = false; else result = a / b; break;
        case TOK_PERCENT: if (b == 0) can_fold = false; else result = a % b; break;
        case TOK_AMP:     result = a & b; break;
        case TOK_PIPE:    result = a | b; break;
        case TOK_CARET:   result = a ^ b; break;
        case TOK_SHL:     result = a << b; break;
        case TOK_SHR:     result = (unsigned)a >> b; break;
        case TOK_EQ:      result = a == b ? 1 : 0; break;
        case TOK_NEQ:     result = a != b ? 1 : 0; break;
        case TOK_LT:      result = a < b ? 1 : 0; break;
        case TOK_GT:      result = a > b ? 1 : 0; break;
        case TOK_LTE:     result = a <= b ? 1 : 0; break;
        case TOK_GTE:     result = a >= b ? 1 : 0; break;
        default: can_fold = false; break;
    }
    
    if (can_fold) {
        emit_push(cg, (uint32_t)result);
        return true;
    }
    return false;
}

/* Symbol → local address mapping */

static int sym_addr(Symbol *s) {
    return s->offset; /* already assigned by symtable */
}

/* Expression codegen */
/* Evaluates expression, result left on top of data stack.              */

static void codegen_expr(Codegen *cg, ASTNode *node, SymTable *st);

static void codegen_expr(Codegen *cg, ASTNode *node, SymTable *st) {
    if (!node) { emit_push(cg, 0); return; }

    switch (node->kind) {

    case AST_INT_LIT:
        if (node->type && node->type->kind == TYPE_DOUBLE) {
            /* Push double as 2 words: hi then lo */
            uint64_t bits;
            memcpy(&bits, &node->as.double_val, 8);
            emit_push(cg, (uint32_t)(bits >> 32));   /* hi */
            emit_push(cg, (uint32_t)(bits & 0xFFFFFFFF)); /* lo */
        } else {
            emit_push(cg, (uint32_t)node->as.int_val);
        }
        break;

    case AST_CHAR_LIT:
        emit_push(cg, (uint32_t)(unsigned char)node->as.char_val);
        break;

    case AST_STRING_LIT: {
        int addr = add_string(cg, node->as.str_val, strlen(node->as.str_val));
        emit_push(cg, (uint32_t)(cg->code_len + addr)); /* absolute byte addr */
        break;
    }

    case AST_IDENT: {
        Symbol *s = symtable_lookup(st, node->as.ident);
        if (!s) {
            fprintf(stderr, "s32-cc: undefined '%s' at line %d\n",
                    node->as.ident, node->line);
            emit_push(cg, 0);
            break;
        }
        if (s->kind == SYM_FUNC) {
            /* Function pointer - push address (placeholder) */
            emit_push(cg, 0);
            break;
        }
        /* Array, struct, and union variables decay to pointer (address) */
        if (s->type && (s->type->kind == TYPE_ARRAY || 
            s->type->kind == TYPE_STRUCT || s->type->kind == TYPE_UNION)) {
            if (s->kind == SYM_LOCAL || s->kind == SYM_PARAM) {
                emit_op(cg, OP_GET_FP);
                emit_push(cg, (uint32_t)sym_addr(s));
                emit_op(cg, OP_ADD);
            } else {
                emit_push(cg, (uint32_t)sym_addr(s));
            }
            break;
        }
        /* Double variables span 2 words: push hi then lo */
        if (s->type && s->type->kind == TYPE_DOUBLE) {
            emit_local_get(cg, sym_addr(s) + 1); /* hi */
            emit_local_get(cg, sym_addr(s));     /* lo */
            break;
        }
        emit_local_get(cg, sym_addr(s));
        break;
    }

    case AST_BINARY: {
        int op = node->as.binary.op;
        bool is_unsigned = node->type && node->type->is_unsigned;

        /* Short-circuit logical AND */
        if (op == TOK_LAND) {
            codegen_expr(cg, node->as.binary.left, st);
            emit_op(cg, OP_DUP);
            int patch_br = cg->code_len;
            emit_op(cg, OP_BR_IF);
            emit_u32(cg, 0);
            emit_op(cg, OP_DROP);
            emit_push(cg, 0);
            int patch_jmp = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            patch_jump(cg, patch_br, cg->code_len);
            emit_op(cg, OP_DROP);
            codegen_expr(cg, node->as.binary.right, st);
            emit_op(cg, OP_EQZ);
            emit_op(cg, OP_EQZ);
            patch_jump(cg, patch_jmp, cg->code_len);
            break;
        }

        /* Short-circuit logical OR */
        if (op == TOK_LOR) {
            codegen_expr(cg, node->as.binary.left, st);
            emit_op(cg, OP_DUP);
            int patch_br = cg->code_len;
            emit_op(cg, OP_BR_IF);
            emit_u32(cg, 0);
            emit_op(cg, OP_DROP);
            codegen_expr(cg, node->as.binary.right, st);
            emit_op(cg, OP_EQZ);
            emit_op(cg, OP_EQZ);
            int patch_jmp = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            patch_jump(cg, patch_br, cg->code_len);
            emit_op(cg, OP_DROP);
            emit_push(cg, 1);
            patch_jump(cg, patch_jmp, cg->code_len);
            break;
        }

        /* Try constant folding first */
        if (!is_unsigned && fold_const_binary(cg, op, node->as.binary.left, node->as.binary.right))
            break;

        /* Algebraic simplifications for known operands */
        if (is_const_int(node->as.binary.right)) {
            int val = get_const_int(node->as.binary.right);
            if (op == TOK_PLUS && val == 0) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
            if (op == TOK_MINUS && val == 0) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
            if (op == TOK_STAR && val == 1) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
            if (op == TOK_SLASH && val == 1) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
            if (op == TOK_SHL && val == 0) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
            if (op == TOK_SHR && val == 0) {
                codegen_expr(cg, node->as.binary.left, st); break;
            }
        }
        if (is_const_int(node->as.binary.left)) {
            int val = get_const_int(node->as.binary.left);
            if (op == TOK_PLUS && val == 0) {
                codegen_expr(cg, node->as.binary.right, st); break;
            }
            if (op == TOK_STAR && val == 1) {
                codegen_expr(cg, node->as.binary.right, st); break;
            }
            if (op == TOK_STAR && val == 0) {
                emit_push(cg, 0); break;
            }
            if (op == TOK_AMP && val == 0) {
                emit_push(cg, 0); break;
            }
            if (op == TOK_PIPE && val == -1) {
                emit_push(cg, 0xFFFFFFFF); break;
            }
        }

        /* Regular binary: eval left, eval right, op */
        codegen_expr(cg, node->as.binary.left, st);
        codegen_expr(cg, node->as.binary.right, st);

        /* Double arithmetic: use FPU opcodes */
        if (node->type && node->type->kind == TYPE_DOUBLE) {
            switch (op) {
                case TOK_PLUS:  emit_op(cg, OP_FADD); break;
                case TOK_MINUS: emit_op(cg, OP_FSUB); break;
                case TOK_STAR:  emit_op(cg, OP_FMUL); break;
                case TOK_SLASH: emit_op(cg, OP_FDIV); break;
                case TOK_EQ:    emit_op(cg, OP_FCMP); emit_op(cg, OP_EQZ); emit_op(cg, OP_EQZ); break;
                case TOK_NEQ:   emit_op(cg, OP_FCMP); emit_op(cg, OP_EQZ); break;
                case TOK_LT:    emit_op(cg, OP_FCMP); emit_push(cg, 0xFFFFFFFF); emit_op(cg, OP_EQ); break;
                case TOK_GT:    emit_op(cg, OP_FCMP); emit_push(cg, 1); emit_op(cg, OP_EQ); break;
                case TOK_LTE:   emit_op(cg, OP_FCMP); emit_push(cg, 1); emit_op(cg, OP_LT_S); emit_op(cg, OP_EQZ); break;
                case TOK_GTE:   emit_op(cg, OP_FCMP); emit_push(cg, 0xFFFFFFFF); emit_op(cg, OP_GT_S); emit_op(cg, OP_EQZ); break;
                default: break;
            }
            break;
        }

        /* Pointer arithmetic: scale the integer operand by element size */
        if ((op == TOK_PLUS || op == TOK_MINUS) && node->type && node->type->kind == TYPE_PTR) {
            int elem_size = type_sizeof(node->type->base);
            int word_scale = elem_size / 4;
            if (word_scale > 1) {
                /* Apply scaling to the second operand (the integer) */
                if (op == TOK_MINUS) {
                    /* For ptr - n: scale n then sub */
                    emit_push(cg, (uint32_t)(word_scale == 2 ? 1 : 2));
                    if (word_scale == 2 || word_scale == 4) {
                        int n = word_scale == 2 ? 1 : 2;
                        for (int k = 0; k < n; k++) emit_op(cg, OP_SHL);
                    } else {
                        emit_push(cg, (uint32_t)word_scale);
                        emit_op(cg, OP_MUL);
                    }
                    emit_op(cg, OP_SUB);
                    break;
                } else {
                    /* For ptr + n or n + ptr: scale the integer first, then add */
                    emit_op(cg, OP_SWAP);
                    emit_push(cg, (uint32_t)(word_scale == 2 ? 1 : 2));
                    if (word_scale == 2 || word_scale == 4) {
                        int n = word_scale == 2 ? 1 : 2;
                        for (int k = 0; k < n; k++) emit_op(cg, OP_SHL);
                    } else {
                        emit_push(cg, (uint32_t)word_scale);
                        emit_op(cg, OP_MUL);
                    }
                    emit_op(cg, OP_SWAP);
                    emit_op(cg, OP_ADD);
                    break;
                }
            }
        }

        switch (op) {
            case TOK_PLUS:    emit_op(cg, OP_ADD); break;
            case TOK_MINUS:   emit_op(cg, OP_SUB); break;
            case TOK_STAR:    emit_op(cg, OP_MUL); break;
            case TOK_SLASH:
                if (is_unsigned) { /* unsigned div not available yet; use DIV_S for now */ emit_op(cg, OP_DIV_S); }
                else { emit_op(cg, OP_DIV_S); }
                break;
            case TOK_PERCENT:
                if (is_unsigned) {
                    emit_op(cg, OP_OVER); emit_op(cg, OP_OVER);
                    emit_op(cg, OP_DIV_S); emit_op(cg, OP_MUL); emit_op(cg, OP_SUB);
                } else {
                    emit_op(cg, OP_OVER); emit_op(cg, OP_OVER);
                    emit_op(cg, OP_DIV_S); emit_op(cg, OP_MUL); emit_op(cg, OP_SUB);
                }
                break;
            case TOK_AMP:     emit_op(cg, OP_AND); break;
            case TOK_PIPE:    emit_op(cg, OP_OR); break;
            case TOK_CARET:   emit_op(cg, OP_XOR); break;
            case TOK_SHL:     emit_op(cg, OP_SHL); break;
            case TOK_SHR:     emit_op(cg, is_unsigned ? OP_SHR_U : OP_SHR_S); break;
            case TOK_EQ:      emit_op(cg, OP_EQ); break;
            case TOK_NEQ:     emit_op(cg, OP_EQ); emit_op(cg, OP_EQZ); break;
            case TOK_LT:
                emit_op(cg, is_unsigned ? OP_LT_U : OP_LT_S);
                break;
            case TOK_GT:
                emit_op(cg, is_unsigned ? OP_GT_U : OP_GT_S);
                break;
            case TOK_LTE:
                emit_op(cg, is_unsigned ? OP_GT_U : OP_GT_S);
                emit_op(cg, OP_EQZ);
                break;
            case TOK_GTE:
                emit_op(cg, is_unsigned ? OP_LT_U : OP_LT_S);
                emit_op(cg, OP_EQZ);
                break;
            default:
                fprintf(stderr, "s32-cc: unhandled binary op %d\n", op);
                break;
        }
        break;
    }

    case AST_CAST: {
        codegen_expr(cg, node->as.unary.expr, st);
        Type *from = node->as.unary.expr->type;
        Type *to = node->type;
        if (from && to && from->kind == TYPE_DOUBLE && to->kind != TYPE_DOUBLE)
            emit_op(cg, OP_F2I);
        else if (from && to && from->kind != TYPE_DOUBLE && to->kind == TYPE_DOUBLE)
            emit_op(cg, OP_I2F);
        break;
    }

    case AST_UNARY: {
        codegen_expr(cg, node->as.unary.expr, st);
        switch (node->as.unary.op) {
            case TOK_MINUS:
                emit_push(cg, 0);
                emit_op(cg, OP_SWAP);
                emit_op(cg, OP_SUB);
                break;
            case TOK_BANG:
                emit_op(cg, OP_EQZ);
                break;
            case TOK_TILDE:
                emit_op(cg, OP_NOT);
                break;
            default: break;
        }
        break;
    }

    case AST_DEREF:
        codegen_expr(cg, node->as.unary.expr, st);
        emit_op(cg, OP_LOAD);
        break;

    case AST_MEMBER: {
        /* Evaluate struct address, then add member byte offset (in words) */
        codegen_expr(cg, node->as.member.obj, st);
        if (node->as.member.member && node->as.member.member->offset > 0) {
            emit_push(cg, (uint32_t)(node->as.member.member->offset / 4));
            emit_op(cg, OP_ADD);
        }
        emit_op(cg, OP_LOAD);
        break;
    }

    case AST_ADDR:
        if (node->as.unary.expr->kind == AST_IDENT) {
            Symbol *s = symtable_lookup(st, node->as.unary.expr->as.ident);
            if (s && (s->kind == SYM_LOCAL || s->kind == SYM_PARAM)) {
                emit_op(cg, OP_GET_FP);
                emit_push(cg, (uint32_t)sym_addr(s));
                emit_op(cg, OP_ADD);
            } else if (s && s->kind == SYM_GLOBAL) {
                emit_push(cg, (uint32_t)sym_addr(s));
            } else {
                fprintf(stderr, "s32-cc: cannot take address of '%s'\n",
                        node->as.unary.expr->as.ident);
                emit_push(cg, 0);
            }
        } else if (node->as.unary.expr->kind == AST_DEREF) {
            /* &*ptr == ptr */
            codegen_expr(cg, node->as.unary.expr->as.unary.expr, st);
        } else {
            fprintf(stderr, "s32-cc: & operator not supported for this expression\n");
            emit_push(cg, 0);
        }
        break;

    case AST_ASSIGN: {
        /* lvalue = rvalue */
        bool through_ptr = (node->as.assign.lvalue->kind == AST_DEREF);
        bool through_index = (node->as.assign.lvalue->kind == AST_INDEX);
        bool through_member = (node->as.assign.lvalue->kind == AST_MEMBER);
        Symbol *s = NULL;
        if (node->as.assign.lvalue->kind == AST_IDENT)
            s = symtable_lookup(st, node->as.assign.lvalue->as.ident);

        if (node->as.assign.op == TOK_ASSIGN) {
            codegen_expr(cg, node->as.assign.rvalue, st);

            if (through_ptr) {
                emit_op(cg, OP_DUP);
                codegen_expr(cg, node->as.assign.lvalue->as.unary.expr, st);
                emit_op(cg, OP_STORE);
            } else if (through_index) {
                emit_op(cg, OP_DUP);
                codegen_expr(cg, node->as.assign.lvalue->as.index.base, st);
                codegen_expr(cg, node->as.assign.lvalue->as.index.index, st);
                if (node->as.assign.lvalue->type) {
                    int ws = (type_sizeof(node->as.assign.lvalue->type) + 3) / 4;
                    if (ws > 1) {
                        emit_push(cg, (uint32_t)(ws == 2 ? 1 : 2));
                        for (int k = 0; k < (ws == 2 ? 1 : ws == 4 ? 2 : 0); k++)
                            emit_op(cg, OP_SHL);
                        if (ws != 2 && ws != 4) {
                            emit_push(cg, (uint32_t)ws);
                            emit_op(cg, OP_MUL);
                        }
                    }
                }
                emit_op(cg, OP_ADD);
                emit_op(cg, OP_STORE);
            } else if (through_member) {
                /* s.member = val: DUP rval, compute member addr, STORE */
                emit_op(cg, OP_DUP);
                codegen_expr(cg, node->as.assign.lvalue->as.member.obj, st);
                if (node->as.assign.lvalue->as.member.member &&
                    node->as.assign.lvalue->as.member.member->offset > 0) {
                    emit_push(cg, (uint32_t)(node->as.assign.lvalue->as.member.member->offset / 4));
                    emit_op(cg, OP_ADD);
                }
                emit_op(cg, OP_STORE);
            } else if (s && (s->kind == SYM_LOCAL || s->kind == SYM_PARAM || s->kind == SYM_GLOBAL)) {
                if (s->type && s->type->kind == TYPE_DOUBLE) {
                    emit_local_set(cg, sym_addr(s));
                    emit_local_set(cg, sym_addr(s) + 1);
                    emit_local_get(cg, sym_addr(s) + 1);
                    emit_local_get(cg, sym_addr(s));
                } else {
                    emit_local_set(cg, sym_addr(s));
                    emit_local_get(cg, sym_addr(s));
                }
            } else {
                fprintf(stderr, "s32-cc: assignment to non-lvalue at line %d\n", node->line);
            }
        } else {
            /* Compound assignment: x op= expr
             * We need: evaluate expr, load old x, perform op, store back, leave result */
            if (s && (s->kind == SYM_LOCAL || s->kind == SYM_PARAM || s->kind == SYM_GLOBAL)) {
                int addr = sym_addr(s);
                /* Evaluate RHS */
                codegen_expr(cg, node->as.assign.rvalue, st);
                /* Stack: [rhs] */
                emit_local_get(cg, addr);
                /* Stack: [rhs, old] */
                switch (node->as.assign.op) {
                    case TOK_PLUS_EQ:  emit_op(cg, OP_ADD); break;
                    case TOK_MINUS_EQ: emit_op(cg, OP_SWAP); emit_op(cg, OP_SUB); break;
                    case TOK_STAR_EQ:  emit_op(cg, OP_MUL); break;
                    case TOK_SLASH_EQ: emit_op(cg, OP_SWAP); emit_op(cg, OP_DIV_S); break;
                    case TOK_PERCENT_EQ:
                        emit_op(cg, OP_SWAP);
                        emit_op(cg, OP_OVER); emit_op(cg, OP_OVER);
                        emit_op(cg, OP_DIV_S);
                        emit_op(cg, OP_MUL); emit_op(cg, OP_SUB);
                        break;
                    case TOK_AMP_EQ:   emit_op(cg, OP_AND); break;
                    case TOK_PIPE_EQ:  emit_op(cg, OP_OR); break;
                    case TOK_CARET_EQ: emit_op(cg, OP_XOR); break;
                    case TOK_SHL_EQ:   emit_op(cg, OP_SWAP); emit_op(cg, OP_SHL); break;
                    case TOK_SHR_EQ:   emit_op(cg, OP_SWAP); emit_op(cg, OP_SHR_S); break;
                    default: break;
                }
                /* Stack: [result] */
                emit_op(cg, OP_DUP);
                emit_local_set(cg, addr);
                /* Stack: [result] */
            } else {
                fprintf(stderr, "s32-cc: assignment to non-lvalue at line %d\n", node->line);
                codegen_expr(cg, node->as.assign.rvalue, st);
            }
        }
        break;
    }

    case AST_CALL: {
        if (node->as.call.func->kind == AST_IDENT) {
            const char *name = node->as.call.func->as.ident;

            /* Built-in intrinsics */
            if (strcmp(name, "putchar") == 0 || strcmp(name, "print_char") == 0) {
                if (node->as.call.arg_count >= 1) {
                    codegen_expr(cg, node->as.call.args[0], st);
                    emit_op(cg, OP_PRINT);
                }
                emit_push(cg, 0); /* dummy for expression-statement DROP */
                break;
            }
            if (strcmp(name, "getchar") == 0) {
                emit_op(cg, OP_KEY);
                break;
            }
            if (strcmp(name, "halt") == 0) {
                emit_op(cg, OP_HALT);
                emit_push(cg, 0);
                break;
            }
            if (strcmp(name, "print_num") == 0) {
                if (node->as.call.arg_count >= 1) {
                    codegen_expr(cg, node->as.call.args[0], st);
                    emit_op(cg, OP_PRINT);
                }
                emit_push(cg, 0); /* dummy for expression-statement DROP */
                break;
            }

            /* Regular function call - push args L-to-R, callee pops in reverse */
            for (int i = 0; i < node->as.call.arg_count; i++)
                codegen_expr(cg, node->as.call.args[i], st);

            emit_push_frame_size(cg);
            int call_pos = cg->code_len;
            emit_op(cg, OP_CALL);
            add_fixup(cg, call_pos, name);
            emit_u32(cg, 0); /* placeholder */
        }
        break;
    }

    case AST_POSTINC: {
        Symbol *s = NULL;
        if (node->as.unary.expr->kind == AST_IDENT)
            s = symtable_lookup(st, node->as.unary.expr->as.ident);
        if (s) {
            emit_local_get(cg, sym_addr(s));
            emit_op(cg, OP_DUP);
            emit_push(cg, 1);
            emit_op(cg, OP_ADD);
            emit_local_set(cg, sym_addr(s));
        }
        break;
    }

    case AST_POSTDEC: {
        Symbol *s = NULL;
        if (node->as.unary.expr->kind == AST_IDENT)
            s = symtable_lookup(st, node->as.unary.expr->as.ident);
        if (s) {
            emit_local_get(cg, sym_addr(s));
            emit_op(cg, OP_DUP);
            emit_push(cg, 1);
            emit_op(cg, OP_SUB);
            emit_local_set(cg, sym_addr(s));
        }
        break;
    }

    case AST_PREINC: {
        Symbol *s = NULL;
        if (node->as.unary.expr->kind == AST_IDENT)
            s = symtable_lookup(st, node->as.unary.expr->as.ident);
        if (s) {
            emit_local_get(cg, sym_addr(s));
            emit_push(cg, 1);
            emit_op(cg, OP_ADD);
            emit_op(cg, OP_DUP);
            emit_local_set(cg, sym_addr(s));
        }
        break;
    }

    case AST_PREDEC: {
        Symbol *s = NULL;
        if (node->as.unary.expr->kind == AST_IDENT)
            s = symtable_lookup(st, node->as.unary.expr->as.ident);
        if (s) {
            emit_local_get(cg, sym_addr(s));
            emit_push(cg, 1);
            emit_op(cg, OP_SUB);
            emit_op(cg, OP_DUP);
            emit_local_set(cg, sym_addr(s));
        }
        break;
    }

    case AST_INDEX: {
        codegen_expr(cg, node->as.index.base, st);
        codegen_expr(cg, node->as.index.index, st);
        if (node->type) {
            int elem_size = type_sizeof(node->type);
            int word_scale = (elem_size + 3) / 4;
            if (word_scale > 1) {
                emit_push(cg, (uint32_t)(word_scale == 2 ? 1 : word_scale == 4 ? 2 : 0));
                if (word_scale == 2) emit_op(cg, OP_SHL);
                else if (word_scale == 4) emit_op(cg, OP_SHL), emit_op(cg, OP_SHL);
                else { emit_push(cg, (uint32_t)word_scale); emit_op(cg, OP_MUL); }
            }
        }
        emit_op(cg, OP_ADD);
        emit_op(cg, OP_LOAD);
        break;
    }

    case AST_TERNARY: {
        /* cond ? then : else */
        codegen_expr(cg, node->as.if_.cond, st);
        emit_op(cg, OP_EQZ);
        int patch_br = cg->code_len;
        emit_op(cg, OP_BR_IF);
        emit_u32(cg, 0); /* jump to else branch */
        /* true branch */
        codegen_expr(cg, node->as.if_.then_body, st);
        int patch_jmp = cg->code_len;
        emit_op(cg, OP_JUMP);
        emit_u32(cg, 0); /* jump past else */
        /* false branch */
        patch_jump(cg, patch_br, cg->code_len);
        codegen_expr(cg, node->as.if_.else_body, st);
        /* end */
        patch_jump(cg, patch_jmp, cg->code_len);
        break;
    }

    default:
        fprintf(stderr, "s32-cc: unhandled expr kind %d at line %d\n",
                node->kind, node->line);
        emit_push(cg, 0);
        break;
    }
}

/* Statement codegen */

static void codegen_stmt(Codegen *cg, ASTNode *node, SymTable *st);

static void codegen_stmt(Codegen *cg, ASTNode *node, SymTable *st) {
    if (!node) return;

    switch (node->kind) {

    case AST_EXPR_STMT:
        codegen_expr(cg, node, st);
        emit_op(cg, OP_DROP);
        break;

    case AST_VAR_DECL:
        {
            Symbol *s = symtable_lookup_local(st, node->as.var_decl.name);
            if (!s) {
                s = symtable_insert(st, node->as.var_decl.name, node->as.var_decl.type, SYM_LOCAL);
                s->offset = cg->next_var_addr;
                int slots = type_sizeof(node->as.var_decl.type) / 4;
                if (slots < 1) slots = 1;
                cg->next_var_addr += slots;
            }
            if (node->as.var_decl.init) {
                if (node->as.var_decl.init->kind == AST_STRING_LIT &&
                    node->as.var_decl.type && node->as.var_decl.type->kind == TYPE_ARRAY) {
                    const char *str = node->as.var_decl.init->as.str_val;
                    int len = strlen(str) + 1;
                    for (int i = 0; i < len; i++) {
                        emit_push(cg, (uint32_t)(unsigned char)str[i]);
                        emit_op(cg, OP_GET_FP);
                        emit_push(cg, (uint32_t)(sym_addr(s) + i));
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_STORE);
                    }
                } else if (node->as.var_decl.init->kind == AST_BLOCK &&
                           node->as.var_decl.type && node->as.var_decl.type->kind == TYPE_ARRAY) {
                    /* Initializer list: { val1, val2, ... } */
                    for (int i = 0; i < node->as.var_decl.init->as.block.count; i++) {
                        codegen_expr(cg, node->as.var_decl.init->as.block.stmts[i], st);
                        emit_op(cg, OP_GET_FP);
                        emit_push(cg, (uint32_t)(sym_addr(s) + i));
                        emit_op(cg, OP_ADD);
                        emit_op(cg, OP_STORE);
                    }
                } else {
                    codegen_expr(cg, node->as.var_decl.init, st);
                    if (node->as.var_decl.type && node->as.var_decl.type->kind == TYPE_DOUBLE) {
                        emit_local_set(cg, sym_addr(s));
                        emit_local_set(cg, sym_addr(s) + 1);
                    } else {
                        emit_local_set(cg, sym_addr(s));
                    }
                }
            } else {
                emit_push(cg, 0);
                if (node->as.var_decl.type && node->as.var_decl.type->kind == TYPE_DOUBLE) {
                    emit_push(cg, 0);
                    emit_local_set(cg, sym_addr(s));
                    emit_local_set(cg, sym_addr(s) + 1);
                } else {
                    emit_local_set(cg, sym_addr(s));
                }
            }
        }
        break;

    case AST_BLOCK: {
        SymTable *block_st = symtable_new(st);
        for (int i = 0; i < node->as.block.count; i++)
            codegen_stmt(cg, node->as.block.stmts[i], block_st);
        symtable_free(block_st);
        break;
    }

    case AST_RETURN:
        if (node->as.ret.expr)
            codegen_expr(cg, node->as.ret.expr, st);
        else
            emit_push(cg, 0);
        emit_op(cg, OP_RETURN);
        break;

    case AST_IF: {
        codegen_expr(cg, node->as.if_.cond, st);
        emit_op(cg, OP_EQZ); /* flip: skip body when cond is 0 */
        int patch_br = cg->code_len;
        emit_op(cg, OP_BR_IF);
        emit_u32(cg, 0); /* jump to else/end if cond was 0 */
        /* BR_IF consumed the EQZ result */
        codegen_stmt(cg, node->as.if_.then_body, st);

        if (node->as.if_.else_body) {
            int patch_jmp = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0); /* jump over else */
            patch_jump(cg, patch_br, cg->code_len);
            codegen_stmt(cg, node->as.if_.else_body, st);
            patch_jump(cg, patch_jmp, cg->code_len);
        } else {
            patch_jump(cg, patch_br, cg->code_len);
        }
        break;
    }

    case AST_WHILE: {
        /* Layout:
         *   :cont_label (loop_top - continue lands here)
         *     cond → EQZ → BR_IF :brk_label
         *     body
         *     JUMP :cont_label
         *   :brk_label (break lands here)
         */
        add_label(cg, node->as.while_.cont_label);
        codegen_expr(cg, node->as.while_.cond, st);
        emit_op(cg, OP_EQZ);
        int patch_br = cg->code_len;
        emit_op(cg, OP_BR_IF);
        emit_u32(cg, 0);
        codegen_stmt(cg, node->as.while_.body, st);
        int jmp_pos = cg->code_len;
        emit_op(cg, OP_JUMP);
        emit_u32(cg, 0);
        add_fixup(cg, jmp_pos, node->as.while_.cont_label);
        patch_jump(cg, patch_br, cg->code_len);
        add_label(cg, node->as.while_.brk_label);
        break;
    }

    case AST_FOR: {
        /* Layout (chibicc pattern):
         *   init
         *   :loop_top
         *     cond → EQZ → BR_IF :brk_label
         *     body
         *   :cont_label (continue lands here - before inc)
         *     inc
         *     JUMP :loop_top
         *   :brk_label (break lands here)
         */
        SymTable *for_st = symtable_new(st);
        if (node->as.for_.init)
            codegen_stmt(cg, node->as.for_.init, for_st);

        char loop_top_label[64];
        snprintf(loop_top_label, sizeof(loop_top_label), ".Lfor_top_%p", (void*)node);
        add_label(cg, loop_top_label);

        int patch_br = -1;
        if (node->as.for_.cond) {
            codegen_expr(cg, node->as.for_.cond, for_st);
            emit_op(cg, OP_EQZ);
            patch_br = cg->code_len;
            emit_op(cg, OP_BR_IF);
            emit_u32(cg, 0);
        }

        codegen_stmt(cg, node->as.for_.body, for_st);

        /* Continue label: before increment */
        add_label(cg, node->as.for_.cont_label);
        if (node->as.for_.inc) {
            codegen_expr(cg, node->as.for_.inc, for_st);
            emit_op(cg, OP_DROP);
        }
        int jmp_pos = cg->code_len;
        emit_op(cg, OP_JUMP);
        emit_u32(cg, 0);
        add_fixup(cg, jmp_pos, loop_top_label);

        if (patch_br >= 0)
            patch_jump(cg, patch_br, cg->code_len);

        add_label(cg, node->as.for_.brk_label);

        symtable_free(for_st);
        break;
    }

    case AST_DO_WHILE: {
        /* Layout:
         *   :loop_top_label (body start - JUMP goes back here)
         *     body
         *   :cont_label (continue lands here - condition check)
         *     cond → EQZ → BR_IF :brk_label
         *     JUMP :loop_top_label
         *   :brk_label (break lands here)
         */
        add_label(cg, node->as.do_while.loop_top_label);
        codegen_stmt(cg, node->as.do_while.body, st);
        add_label(cg, node->as.do_while.cont_label);
        codegen_expr(cg, node->as.do_while.cond, st);
        emit_op(cg, OP_EQZ);
        int patch_br = cg->code_len;
        emit_op(cg, OP_BR_IF);
        emit_u32(cg, 0);
        int jmp_pos = cg->code_len;
        emit_op(cg, OP_JUMP);
        emit_u32(cg, 0);
        add_fixup(cg, jmp_pos, node->as.do_while.loop_top_label);
        patch_jump(cg, patch_br, cg->code_len);
        add_label(cg, node->as.do_while.brk_label);
        break;
    }

    case AST_BREAK:
        if (node->as.jump_label.label) {
            int jmp_pos = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            add_fixup(cg, jmp_pos, node->as.jump_label.label);
        }
        break;

    case AST_CONTINUE:
        if (node->as.jump_label.label) {
            int jmp_pos = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            add_fixup(cg, jmp_pos, node->as.jump_label.label);
        }
        break;

    case AST_NULL_STMT:
        break;

    case AST_LABEL: {
        char label_name[256];
        snprintf(label_name, sizeof(label_name), ".L%s", node->as.label.name);
        add_label(cg, label_name);
        codegen_stmt(cg, node->as.label.stmt, st);
        break;
    }

    case AST_GOTO: {
        char label_name[256];
        snprintf(label_name, sizeof(label_name), ".L%s", node->as.goto_.target);
        int pos = cg->code_len;
        emit_op(cg, OP_JUMP);
        emit_u32(cg, 0);
        add_fixup(cg, pos, label_name);
        break;
    }

    case AST_SWITCH: {
        /* chibicc approach: linear scan of case_next linked list.
         * Layout:
         *   eval expr → push val
         *   DUP + PUSH val1 + EQ + BR_IF :case_label1
         *   DUP + PUSH val2 + EQ + BR_IF :case_label2
         *   ...
         *   JUMP :default_label or :brk_label
         *   body (case labels are just labels before their bodies)
         *   :brk_label
         *   DROP (remove val from stack)
         */
        codegen_expr(cg, node->as.switch_.expr, st);

        /* Comparison chain */
        for (ASTNode *n = node->as.switch_.case_next; n; n = n->as.case_.case_next) {
            if (n->as.case_.begin == n->as.case_.end) {
                /* Single case value */
                emit_op(cg, OP_DUP);
                emit_push(cg, (uint32_t)n->as.case_.begin);
                emit_op(cg, OP_EQ);
                int br_pos = cg->code_len;
                emit_op(cg, OP_BR_IF);
                emit_u32(cg, 0);
                add_fixup(cg, br_pos, n->as.case_.label);
            } else {
                /* Case range: begin..end → DUP-PUSH(begin)-SUB → PUSH(end-begin)-JBE */
                emit_op(cg, OP_DUP);
                emit_push(cg, (uint32_t)n->as.case_.begin);
                emit_op(cg, OP_SUB);
                emit_push(cg, (uint32_t)(n->as.case_.end - n->as.case_.begin));
                emit_op(cg, OP_GT_U); /* if (val - begin) > (end - begin) → not in range */
                int br_pos = cg->code_len;
                emit_op(cg, OP_BR_IF);
                emit_u32(cg, 0); /* skip if NOT in range */
                /* In range - we inverted the condition, so BR_IF skips.
                 * We need to NOT skip. So emit JUMP to case label, then patch BR to skip past it */
                emit_op(cg, OP_JUMP);
                emit_u32(cg, 0);
                add_fixup(cg, cg->code_len - 4, n->as.case_.label);
                patch_jump(cg, br_pos, cg->code_len);
            }
        }

        /* No match: jump to default or break */
        if (node->as.switch_.default_case) {
            int def_jmp = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            add_fixup(cg, def_jmp, node->as.switch_.default_case->as.case_.label);
        } else {
            int brk_jmp = cg->code_len;
            emit_op(cg, OP_JUMP);
            emit_u32(cg, 0);
            add_fixup(cg, brk_jmp, node->as.switch_.brk_label);
        }

        /* Emit body - case labels inside will be emitted by AST_CASE handler */
        codegen_stmt(cg, node->as.switch_.body, st);

        /* Break label + drop val */
        add_label(cg, node->as.switch_.brk_label);
        emit_op(cg, OP_DROP);
        break;
    }

    case AST_CASE: {
        /* Emit case label, then body */
        add_label(cg, node->as.case_.label);
        codegen_stmt(cg, node->as.case_.body, st);
        break;
    }

    default:
        /* Expression as statement */
        codegen_expr(cg, node, st);
        emit_op(cg, OP_DROP);
        break;
    }
}

/* Function codegen */

static void codegen_func(Codegen *cg, ASTNode *node, SymTable *st) {
    if (!node->as.func_decl.body) return;

    /* Create function label */
    add_label(cg, node->as.func_decl.name);

    /* Create a child scope for params + locals */
    SymTable *func_st = symtable_new(st);

    /* Assign addresses to params (args arrive on data stack, L-to-R) */
    int param_count = node->as.func_decl.param_count;
    for (int i = 0; i < param_count; i++) {
        if (node->as.func_decl.params[i] &&
            node->as.func_decl.params[i]->kind == AST_VAR_DECL) {
            Symbol *s = symtable_insert(func_st,
                node->as.func_decl.params[i]->as.var_decl.name,
                node->as.func_decl.params[i]->as.var_decl.type,
                SYM_PARAM);
            s->offset = cg->next_var_addr++;
        }
    }

    /* Prologue: pop arguments from stack into their RAM slots.
     * Args are on the stack with param_0 deepest, param_N-1 on top.
     * Pop in reverse order (param_N-1 first). */
    for (int i = param_count - 1; i >= 0; i--) {
        if (!node->as.func_decl.params[i]) continue;
        Symbol *s = symtable_lookup(func_st,
            node->as.func_decl.params[i]->as.var_decl.name);
        if (s) emit_local_set(cg, sym_addr(s));
    }

    ASTNode *body = node->as.func_decl.body;

    /* Codegen body - AST_VAR_DECL nodes register themselves in func_st on-the-fly */
    for (int i = 0; i < body->as.block.count; i++) {
        ASTNode *stmt = body->as.block.stmts[i];
        if (stmt)
            codegen_stmt(cg, stmt, func_st);
    }

    /* Epilogue: implicit return 0 */
    emit_push(cg, 0);
    emit_op(cg, OP_RETURN);

    symtable_free(func_st);
}

/* Peephole optimizer */

static int read_u32_at(uint8_t *code, int pos) {
    return code[pos] | (code[pos+1] << 8) | (code[pos+2] << 16) | (code[pos+3] << 24);
}

static void write_u32_at(uint8_t *code, int pos, int val) {
    code[pos]   = val & 0xFF;
    code[pos+1] = (val >> 8) & 0xFF;
    code[pos+2] = (val >> 16) & 0xFF;
    code[pos+3] = (val >> 24) & 0xFF;
}

/* Remove a range of bytes by sliding the rest of the buffer */
static void code_remove(Codegen *cg, int pos, int len) {
    memmove(cg->code + pos, cg->code + pos + len, cg->code_len - pos - len);
    cg->code_len -= len;
}

static void peephole_optimize(Codegen *cg) {
    /* Peephole passes disabled - operand bytes can falsely match opcodes.
     * A safe implementation would track instruction boundaries first. */
    (void)read_u32_at;
    (void)write_u32_at;
    (void)code_remove;
    return;
    /* Pass 1: Remove PUSH + DROP pairs */
    for (int i = 0; i < cg->code_len - 5; ) {
        if (cg->code[i] == OP_PUSH && cg->code[i + 5] == OP_DROP) {
            code_remove(cg, i, 6);
        } else i++;
    }

    /* Pass 2: Remove DUP + DROP pairs (safe) */
    for (int i = 0; i < cg->code_len - 1; ) {
        if (cg->code[i] == OP_DUP && cg->code[i + 1] == OP_DROP) {
            code_remove(cg, i, 2);
        } else i++;
    }

    /* Pass 3: Remove SWAP + SWAP pairs (safe) */
    for (int i = 0; i < cg->code_len - 1; ) {
        if (cg->code[i] == OP_SWAP && cg->code[i + 1] == OP_SWAP) {
            code_remove(cg, i, 2);
        } else i++;
    }

    /* Pass 4: Remove local.get x + local.set x (safe - no-op) */
    for (int i = 0; i < cg->code_len - 3; ) {
        if (cg->code[i] == OP_LOCAL_GET && cg->code[i + 2] == OP_LOCAL_SET &&
            cg->code[i + 1] == cg->code[i + 3]) {
            code_remove(cg, i, 4);
        } else i++;
    }

    /* Pass 5: Remove PUSH 0 + ADD (safe - identity) */
    for (int i = 0; i < cg->code_len - 5; ) {
        if (cg->code[i] == OP_PUSH && read_u32_at(cg->code, i+1) == 0 &&
            cg->code[i+5] == OP_ADD) {
            code_remove(cg, i, 6);
        } else i++;
    }
}

/* Top-level program codegen */

void codegen_program(Codegen *cg, ASTNode *node, FILE *out) {
    cg->out = out;

    if (node->kind != AST_BLOCK) return;

    /* First pass: register all function labels (code addresses TBD) */
    /* We need forward references, so do a two-pass approach:
     * Pass 1: collect all function names
     * Pass 2: codegen each function, then resolve fixups */

    /* Pass 1 - dummy to collect names (we'll just use fixups) */
    for (int i = 0; i < node->as.block.count; i++) {
        ASTNode *d = node->as.block.stmts[i];
        if (d && d->kind == AST_FUNC_DECL) {
            /* Will be defined during pass 2 */
        }
    }

    /* Pass 2 - codegen */
    /* We need a global symbol table... let's create one here */
    SymTable *top = symtable_new(NULL);

    /* Emit _start stub: call main, then halt */
    add_label(cg, "_start");
    int saved_var_addr = cg->next_var_addr;
    cg->next_var_addr = 0;
    emit_push_frame_size(cg);   /* push 0; >r */
    cg->next_var_addr = saved_var_addr;
    int call_pos = cg->code_len;
    emit_op(cg, OP_CALL);
    add_fixup(cg, call_pos, "main");
    emit_u32(cg, 0);
    emit_op(cg, OP_HALT);

    /* Codegen all functions and global variables */
    for (int i = 0; i < node->as.block.count; i++) {
        ASTNode *d = node->as.block.stmts[i];
        if (d && d->kind == AST_FUNC_DECL) {
            Type *fty = type_func(d->as.func_decl.return_type, NULL, d->as.func_decl.param_count, false);
            symtable_insert(top, d->as.func_decl.name, fty, SYM_FUNC);
            codegen_func(cg, d, top);
        } else if (d && d->kind == AST_VAR_DECL) {
            Symbol *s = symtable_insert(top, d->as.var_decl.name,
                                         d->as.var_decl.type, SYM_GLOBAL);
            s->offset = cg->next_var_addr++;
            if (d->as.var_decl.init) {
                codegen_expr(cg, d->as.var_decl.init, top);
            } else {
                emit_push(cg, 0);
            }
            emit_local_set(cg, sym_addr(s));
        }
    }

    /* Ensure main() exists */
    if (find_label(cg, "main") < 0) {
        fprintf(stderr, "s32-cc: warning: no main() function\n");
    }

    /* Emit HALT at end */
    emit_op(cg, OP_HALT);

    /* Resolve all forward reference fixups */
    resolve_fixups(cg);

    /* Optimize */
    peephole_optimize(cg);

    /* Write binary */
    fwrite(cg->code, 1, cg->code_len, out);

    symtable_free(top);
}

/* ELF Object file output (-c flag) */

static void obj_write_ehdr(FILE *out, uint16_t type, uint16_t shnum,
                            uint32_t shoff, uint16_t shstrndx, uint32_t entry) {
    Elf32_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[EI_MAG0] = ELFMAG0;
    ehdr.e_ident[EI_MAG1] = ELFMAG1;
    ehdr.e_ident[EI_MAG2] = ELFMAG2;
    ehdr.e_ident[EI_MAG3] = ELFMAG3;
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = type;
    ehdr.e_machine = EM_WASM32;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = entry;
    ehdr.e_phoff = 0;
    ehdr.e_shoff = shoff;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf32_Ehdr);
    ehdr.e_phentsize = sizeof(Elf32_Phdr);
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf32_Shdr);
    ehdr.e_shnum = shnum;
    ehdr.e_shstrndx = shstrndx;
    fwrite(&ehdr, sizeof(ehdr), 1, out);
}

static void obj_write_shdr(FILE *out, uint32_t name, uint32_t type, uint32_t flags,
                            uint32_t offset, uint32_t size, uint32_t link, uint32_t info,
                            uint32_t addralign, uint32_t entsize) {
    Elf32_Shdr shdr;
    memset(&shdr, 0, sizeof(shdr));
    shdr.sh_name = name;
    shdr.sh_type = type;
    shdr.sh_flags = flags;
    shdr.sh_offset = offset;
    shdr.sh_size = size;
    shdr.sh_link = link;
    shdr.sh_info = info;
    shdr.sh_addralign = addralign;
    shdr.sh_entsize = entsize;
    fwrite(&shdr, sizeof(shdr), 1, out);
}

static int obj_strtab_build(const char **strs, int count, uint8_t **out_data) {
    int size = 1;
    for (int i = 0; i < count; i++)
        size += strlen(strs[i]) + 1;
    *out_data = malloc(size);
    int pos = 0;
    (*out_data)[pos++] = '\0';
    for (int i = 0; i < count; i++) {
        int len = strlen(strs[i]);
        memcpy(*out_data + pos, strs[i], len + 1);
        pos += len + 1;
    }
    return size;
}

static int obj_strtab_find(uint8_t *tab, int size, const char *s) {
    int pos = 0;
    while (pos < size) {
        if (strcmp((char*)(tab + pos), s) == 0) return pos;
        pos += strlen((char*)(tab + pos)) + 1;
    }
    return -1;
}

void codegen_program_object(Codegen *cg, ASTNode *node, FILE *out) {
    cg->out = out;
    if (node->kind != AST_BLOCK) return;

    SymTable *top = symtable_new(NULL);

    /* First pass: initialize all global variables */
    for (int i = 0; i < node->as.block.count; i++) {
        ASTNode *d = node->as.block.stmts[i];
        if (d && d->kind == AST_VAR_DECL) {
            Symbol *s = symtable_lookup(top, d->as.var_decl.name);
            if (!s) {
                s = symtable_insert(top, d->as.var_decl.name,
                                     d->as.var_decl.type, SYM_GLOBAL);
                s->offset = cg->next_var_addr++;
            }
            if (d->as.var_decl.init) {
                codegen_expr(cg, d->as.var_decl.init, top);
            }
            emit_local_set(cg, sym_addr(s));
        }
    }

    /* Second pass: codegen all functions */
    for (int i = 0; i < node->as.block.count; i++) {
        ASTNode *d = node->as.block.stmts[i];
        if (d && d->kind == AST_FUNC_DECL) {
            Type *fty = type_func(d->as.func_decl.return_type, NULL,
                                  d->as.func_decl.param_count, false);
            symtable_insert(top, d->as.func_decl.name, fty, SYM_FUNC);
            codegen_func(cg, d, top);
        }
    }
    emit_op(cg, OP_HALT);

    /* Classify labels: non-".L" and non-"_start" → global symbols */
    int *global_label_ids = malloc(sizeof(int) * cg->label_count);
    int global_label_count = 0;
    for (int i = 0; i < cg->label_count; i++) {
        if (cg->labels[i].name[0] != '.' &&
            strcmp(cg->labels[i].name, "_start") != 0) {
            global_label_ids[global_label_count++] = i;
        }
    }

    /* Classify fixups: external ones target labels NOT in labels[] */
    int *ext_fixup_ids = malloc(sizeof(int) * cg->fixup_count);
    int *ext_sym_ids = malloc(sizeof(int) * cg->fixup_count);
    int ext_fixup_count = 0;
    for (int i = 0; i < cg->fixup_count; i++) {
        int addr = find_label(cg, cg->fixups[i].label);
        if (addr < 0) {
            bool dup = false;
            for (int j = 0; j < ext_fixup_count; j++) {
                if (strcmp(cg->fixups[ext_fixup_ids[j]].label,
                           cg->fixups[i].label) == 0) {
                    ext_sym_ids[i] = ext_sym_ids[ext_fixup_ids[j]];
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                ext_sym_ids[i] = ext_fixup_count;
                ext_fixup_ids[ext_fixup_count++] = i;
            }
        }
    }

    /* Build symbol table */
    int sym_null = 1;
    int sym_count = sym_null + global_label_count + ext_fixup_count;
    const char **sym_names = malloc(sizeof(char*) * sym_count);
    sym_names[0] = "";
    for (int i = 0; i < global_label_count; i++)
        sym_names[sym_null + i] = cg->labels[global_label_ids[i]].name;
    for (int i = 0; i < ext_fixup_count; i++)
        sym_names[sym_null + global_label_count + i] =
            cg->fixups[ext_fixup_ids[i]].label;

    /* Build .strtab */
    uint8_t *strtab = NULL;
    int strtab_size = obj_strtab_build(sym_names, sym_count, &strtab);

    /* Build .symtab */
    int symtab_entsize = sizeof(Elf32_Sym);
    int symtab_size = sym_count * symtab_entsize;
    uint8_t *symtab = malloc(symtab_size);
    memset(symtab, 0, symtab_size);

    for (int i = 0; i < global_label_count; i++) {
        Elf32_Sym *s = (Elf32_Sym*)(symtab + (sym_null + i) * symtab_entsize);
        s->st_name = obj_strtab_find(strtab, strtab_size,
                                      cg->labels[global_label_ids[i]].name);
        s->st_value = (uint32_t)cg->labels[global_label_ids[i]].addr;
        s->st_info = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
        s->st_shndx = 1; /* .text section (index 1) */
    }
    for (int i = 0; i < ext_fixup_count; i++) {
        Elf32_Sym *s = (Elf32_Sym*)(symtab + (sym_null + global_label_count + i) * symtab_entsize);
        s->st_name = obj_strtab_find(strtab, strtab_size,
                                      cg->fixups[ext_fixup_ids[i]].label);
        s->st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        s->st_shndx = SHN_UNDEF;
    }

    /* Build .rela.text */
    int rela_count = cg->fixup_count;
    int rela_entsize = sizeof(Elf32_Rela);
    int rela_size = rela_count * rela_entsize;
    uint8_t *rela = malloc(rela_size > 0 ? rela_size : 1);
    memset(rela, 0, rela_size > 0 ? rela_size : 1);

    for (int i = 0; i < cg->fixup_count; i++) {
        Elf32_Rela *r = (Elf32_Rela*)(rela + i * rela_entsize);
        int addr = find_label(cg, cg->fixups[i].label);
        if (addr >= 0) {
            /* Internal reference - resolve in place, still emit rela */
            int sym_idx = -1;
            for (int j = 0; j < global_label_count; j++) {
                if (strcmp(cg->labels[global_label_ids[j]].name,
                           cg->fixups[i].label) == 0) {
                    sym_idx = sym_null + j;
                    break;
                }
            }
            if (sym_idx >= 0) {
                r->r_offset = (uint32_t)(cg->fixups[i].code_pos + 1);
                r->r_info = ELF32_R_INFO(sym_idx, R_WASM32_32);
                r->r_addend = (int32_t)addr;
                /* Patch in code array too */
                int fpos = cg->fixups[i].code_pos;
                cg->code[fpos + 1] = addr & 0xFF;
                cg->code[fpos + 2] = (addr >> 8) & 0xFF;
                cg->code[fpos + 3] = (addr >> 16) & 0xFF;
                cg->code[fpos + 4] = (addr >> 24) & 0xFF;
            }
        } else {
            /* External reference - look up symbol index */
            int sym_idx = -1;
            for (int j = 0; j < ext_fixup_count; j++) {
                if (strcmp(cg->fixups[ext_fixup_ids[j]].label,
                           cg->fixups[i].label) == 0) {
                    sym_idx = sym_null + global_label_count + j;
                    break;
                }
            }
            if (sym_idx >= 0) {
                r->r_offset = (uint32_t)(cg->fixups[i].code_pos + 1);
                r->r_info = ELF32_R_INFO(sym_idx, R_WASM32_32);
                r->r_addend = 0;
            }
        }
    }

    /* Build .shstrtab */
    const char *sec_names[] = {"", ".text", ".symtab", ".strtab",
                                ".shstrtab", ".rela.text"};
    int sec_count = sizeof(sec_names) / sizeof(sec_names[0]);
    uint8_t *shstrtab = NULL;
    int shstrtab_size = obj_strtab_build(sec_names, sec_count, &shstrtab);

    /* Calculate file layout */
    size_t off = sizeof(Elf32_Ehdr);
    size_t text_off = off, text_size = cg->code_len;
    off += text_size;
    while (off % 4 != 0) off++;
    size_t rela_off = off, rela_fsize = rela_size;
    off += rela_fsize;
    size_t symtab_off = off;
    off += symtab_size;
    size_t strtab_off = off;
    off += strtab_size;
    size_t shstrtab_off = off;
    off += shstrtab_size;
    size_t shoff = off;
    int shnum = sec_count;

    /* Write ELF header */
    rewind(out);
    obj_write_ehdr(out, ET_REL, shnum, (uint32_t)shoff, 4, 0);

    /* Write .text */
    fseek(out, (long)text_off, SEEK_SET);
    fwrite(cg->code, 1, text_size, out);

    /* Write .rela.text */
    if (rela_size > 0) {
        fseek(out, (long)rela_off, SEEK_SET);
        fwrite(rela, 1, rela_size, out);
    }

    /* Write .symtab */
    fseek(out, (long)symtab_off, SEEK_SET);
    fwrite(symtab, 1, symtab_size, out);

    /* Write .strtab */
    fseek(out, (long)strtab_off, SEEK_SET);
    fwrite(strtab, 1, strtab_size, out);

    /* Write .shstrtab */
    fseek(out, (long)shstrtab_off, SEEK_SET);
    fwrite(shstrtab, 1, shstrtab_size, out);

    /* Write section headers */
    fseek(out, (long)shoff, SEEK_SET);
    obj_write_shdr(out, 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0); /* NULL */

    int sn_text = obj_strtab_find(shstrtab, shstrtab_size, ".text");
    obj_write_shdr(out, sn_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                   (uint32_t)text_off, (uint32_t)text_size, 0, 0, 1, 0);

    int sn_symtab = obj_strtab_find(shstrtab, shstrtab_size, ".symtab");
    obj_write_shdr(out, sn_symtab, SHT_SYMTAB, 0,
                   (uint32_t)symtab_off, (uint32_t)symtab_size,
                   3, sym_null + global_label_count, 4, symtab_entsize); /* sh_link=.strtab */

    int sn_strtab = obj_strtab_find(shstrtab, shstrtab_size, ".strtab");
    obj_write_shdr(out, sn_strtab, SHT_STRTAB, 0,
                   (uint32_t)strtab_off, (uint32_t)strtab_size, 0, 0, 1, 0);

    int sn_shstrtab = obj_strtab_find(shstrtab, shstrtab_size, ".shstrtab");
    obj_write_shdr(out, sn_shstrtab, SHT_STRTAB, 0,
                   (uint32_t)shstrtab_off, (uint32_t)shstrtab_size, 0, 0, 1, 0);

    int sn_rela = obj_strtab_find(shstrtab, shstrtab_size, ".rela.text");
    obj_write_shdr(out, sn_rela, SHT_RELA, 0,
                   (uint32_t)rela_off, (uint32_t)rela_fsize,
                   2, 1, 4, rela_entsize); /* sh_link=.symtab, sh_info=.text */

    free(rela);
    free(symtab);
    free(strtab);
    free(shstrtab);
    free(sym_names);
    free(global_label_ids);
    free(ext_fixup_ids);
    free(ext_sym_ids);
    symtable_free(top);
}

void codegen_free(Codegen *cg) {
    free(cg->code);
    for (int i = 0; i < cg->label_count; i++)
        free(cg->labels[i].name);
    free(cg->labels);
    for (int i = 0; i < cg->fixup_count; i++)
        free(cg->fixups[i].label);
    free(cg->fixups);
    for (int i = 0; i < cg->string_count; i++)
        free(cg->strings[i].str);
    free(cg->strings);
}
