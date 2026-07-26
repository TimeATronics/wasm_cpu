
    .globl _start
_start:
    push 42
    get_fp
    push 0
    add
    push 2
    shl
    push 0
    add
    store8
    get_fp
    push 0
    add
    push 2
    shl
    push 0
    add
    load8_u
    halt

