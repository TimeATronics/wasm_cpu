
    .globl _start
_start:
    push 100
    >r
    call :test_fp
    halt
test_fp:
    get_fp
    return

