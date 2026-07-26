#ifndef S32CC_CODEGEN_H
#define S32CC_CODEGEN_H

#include "ast.h"
#include "symtable.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* WASM-S32 Opcodes - must match ISA.md / sim.py exactly */
enum {
    OP_NOP       = 0x00,
    OP_PUSH      = 0x01, /* PUSH imm32 */
    OP_ADD       = 0x02,
    OP_SUB       = 0x03,
    OP_MUL       = 0x04,
    OP_DROP      = 0x05,
    OP_PRINT     = 0x08,
    OP_EQ        = 0x09,
    OP_LT_S      = 0x0A,
    OP_GT_S      = 0x0B,
    OP_LT_U      = 0x0C,
    OP_GT_U      = 0x0D,
    OP_BR_IF     = 0x0E, /* BR_IF imm32 */
    OP_JUMP      = 0x0F, /* JUMP imm32 */
    OP_CALL      = 0x10, /* CALL imm32 */
    OP_RETURN    = 0x11,
    OP_DUP       = 0x12,
    OP_SWAP      = 0x13,
    OP_OVER      = 0x14,
    OP_ROT       = 0x15,
    OP_AND       = 0x16,
    OP_OR        = 0x17,
    OP_XOR       = 0x18,
    OP_NOT       = 0x19,
    OP_SHL       = 0x1A,
    OP_SHR_U     = 0x1B,
    OP_SHR_S     = 0x1C,
    OP_LOAD      = 0x1D,
    OP_STORE     = 0x1E,
    OP_KEY       = 0x1F,
    OP_GT_R      = 0x30, /* >r */
    OP_R_GT      = 0x31, /* r> */
    OP_R_COPY    = 0x32, /* r@ */
    OP_DEPTH     = 0x33,
    OP_RDEPTH    = 0x34,
    OP_EQZ       = 0x35,
    OP_DIV_S     = 0x36,
    OP_LOAD8_U   = 0x37,
    OP_STORE8    = 0x38,
    OP_LOCAL_GET = 0x39, /* LOCAL_GET idx8 */
    OP_LOCAL_SET = 0x3A, /* LOCAL_SET idx8 */
    OP_SYSENTER  = 0x3B,
    OP_ERET      = 0x3C,
    OP_CSR_READ  = 0x3D,
    OP_CSR_WRITE = 0x3E,
    OP_TLB_FLUSH = 0x3F,
    OP_GET_FP    = 0x40,
    OP_FADD      = 0x41, OP_FSUB = 0x42, OP_FMUL = 0x43,
    OP_FDIV      = 0x44, OP_FCMP = 0x45, OP_F2I  = 0x46,
    OP_I2F       = 0x47,
    OP_CALL_IND  = 0x48, /* indirect call: pop target, rpush(pc), pc = target */
    OP_HALT      = 0xFF,
};

typedef struct {
    uint8_t *code;
    int code_len;
    int code_cap;

    /* Labels (function entry points) */
    struct { char *name; int addr; } *labels;
    int label_count;
    int label_cap;

    /* Forward reference fixups */
    struct { int code_pos; char *label; } *fixups;
    int fixup_count;
    int fixup_cap;

    /* String literals stored in data segment */
    struct { char *str; int addr; int len; } *strings;
    int string_count;
    int string_cap;
    int data_end;

    /* RAM variable slots - all variables get unique word addresses. */
    int next_var_addr;

    /* Current function's frame size (for CALL prologue) */
    int current_frame_size;

    Type *current_return_type;

    FILE *out;
} Codegen;

void codegen_init(Codegen *cg);
void codegen_program(Codegen *cg, ASTNode *node, FILE *out);
void codegen_program_object(Codegen *cg, ASTNode *node, FILE *out);
void codegen_free(Codegen *cg);

#endif
