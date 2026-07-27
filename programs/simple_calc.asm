; Simple Calculator - Clean version
; Input format: <digit><op><digit> followed by Enter
; Supported: + - * only (no division for now)

:main
    push 62 ; '>'
    print
    push 32
    print

    key     ; digit1_char
    dup
    print
    push 48
    sub     ; digit1
    
    key     ; digit1 op_char
    dup
    print
    
    key     ; digit1 op digit2_char
    dup
    print
    push 48
    sub     ; digit1 op digit2
    
    push 13
    print
    push 10
    print
    
    ; Stack: digit1 op digit2
    ; Rearrange to: digit1 digit2 op
    swap    ; digit1 digit2 op
    
    ; Check if op is '+'
    dup     ; digit1 digit2 op op
    push 43
    eq      ; digit1 digit2 op (op==43)
    br_if :add_op
    
    ; Check if op is '-'
    dup
    push 45
    eq
    br_if :sub_op
    
    ; Check if op is '*'
    dup
    push 42
    eq
    br_if :mul_op
    
    ; Unknown - drop everything
    drop
    drop
    drop
    jump :main

:add_op
    drop    ; digit1 digit2
    add
    push 48
    add
    print
    push 13
    print
    push 10
    print
    jump :main

:sub_op
    drop
    sub
    push 48
    add
    print
    push 13
    print
    push 10
    print
    jump :main

:mul_op
    drop
    mul
    push 48
    add
    print
    push 13
    print
    push 10
    print
    jump :main
