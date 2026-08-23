OP_PUSH     = 0x01
OP_ADD      = 0x02
OP_SUB      = 0x03
OP_MUL      = 0x04
OP_DROP     = 0x05
OP_PRINT    = 0x08
OP_EQ       = 0x09
OP_LT_S     = 0x0A
OP_GT_S     = 0x0B
OP_LT_U     = 0x0C
OP_GT_U     = 0x0D
OP_BR_IF    = 0x0E
OP_JUMP     = 0x0F
OP_CALL     = 0x10
OP_RETURN   = 0x11
OP_DUP      = 0x12
OP_SWAP     = 0x13
OP_OVER     = 0x14
OP_ROT      = 0x15
OP_AND      = 0x16
OP_OR       = 0x17
OP_XOR      = 0x18
OP_NOT      = 0x19
OP_SHL      = 0x1A
OP_SHR_U    = 0x1B
OP_SHR_S    = 0x1C
OP_LOAD     = 0x1D
OP_STORE    = 0x1E
OP_KEY      = 0x1F
OP_TO_R     = 0x30
OP_FROM_R   = 0x31
OP_R_FETCH  = 0x32
OP_DEPTH    = 0x33
OP_R_DEPTH  = 0x34
OP_EQZ      = 0x35
OP_DIV_S    = 0x36
OP_LOAD8_U  = 0x37
OP_STORE8   = 0x38
OP_LOCAL_GET = 0x39
OP_LOCAL_SET = 0x3A
OP_GET_FP   = 0x40
OP_CALL_IND  = 0x48
OP_SET_FP   = 0x49
OP_HALT     = 0xFF

IMM_0 = frozenset({
    OP_ADD, OP_SUB, OP_MUL, OP_DROP, OP_PRINT,
    OP_EQ, OP_LT_S, OP_GT_S, OP_LT_U, OP_GT_U,
    OP_RETURN, OP_DUP, OP_SWAP, OP_OVER, OP_ROT,
    OP_AND, OP_OR, OP_XOR, OP_NOT, OP_SHL, OP_SHR_U, OP_SHR_S,
    OP_LOAD, OP_STORE, OP_KEY,
    OP_TO_R, OP_FROM_R, OP_R_FETCH, OP_DEPTH, OP_R_DEPTH,
    OP_EQZ, OP_DIV_S, OP_LOAD8_U, OP_STORE8,
    OP_GET_FP, OP_CALL_IND, OP_SET_FP, OP_HALT,
})

IMM_1 = frozenset({OP_LOCAL_GET, OP_LOCAL_SET})

IMM_4 = frozenset({OP_PUSH, OP_BR_IF, OP_JUMP, OP_CALL})

MNEMONICS = {
    'push': OP_PUSH, 'add': OP_ADD, 'sub': OP_SUB, 'mul': OP_MUL,
    'drop': OP_DROP, 'print': OP_PRINT,
    'eq': OP_EQ, 'lt_s': OP_LT_S, 'gt_s': OP_GT_S,
    'lt_u': OP_LT_U, 'gt_u': OP_GT_U,
    'br_if': OP_BR_IF, 'jump': OP_JUMP, 'call': OP_CALL, 'return': OP_RETURN,
    'dup': OP_DUP, 'swap': OP_SWAP, 'over': OP_OVER, 'rot': OP_ROT,
    'and': OP_AND, 'or': OP_OR, 'xor': OP_XOR, 'not': OP_NOT,
    'shl': OP_SHL, 'shr_u': OP_SHR_U, 'shr_s': OP_SHR_S,
    'load': OP_LOAD, 'store': OP_STORE, 'key': OP_KEY,
    '>r': OP_TO_R, 'r>': OP_FROM_R, 'r@': OP_R_FETCH,
    'depth': OP_DEPTH, 'rdepth': OP_R_DEPTH,
    'eqz': OP_EQZ, 'div_s': OP_DIV_S,
    'load8_u': OP_LOAD8_U, 'store8': OP_STORE8,
    'local.get': OP_LOCAL_GET, 'local.set': OP_LOCAL_SET,
    'get_fp': OP_GET_FP, 'set_fp': OP_SET_FP, 'halt': OP_HALT,
}