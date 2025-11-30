; Full-Featured Calculator
; Supports: multi-digit numbers, +, -, *, /
; Input format: <number><op><number> (e.g., 123+456)
; Press Enter to calculate

:main
    ; Print prompt
    push 62  ; '>'
    print
    push 32  ; ' '
    print
    
    ; Read line into RAM
    call :readline
    
    ; Print newline
    push 13
    print
    push 10
    print
    
    ; Parse and calculate
    call :parse_calc
    
    jump :main

; Read line into RAM starting at address 0
:readline
    push 0  ; addr
    
:read_loop
    key
    
    ; Check for Enter (CR = 13)
    dup
    push 13
    eq
    br_if :read_done
    
    ; Echo character
    dup
    print
    
    ; Store: need (value addr) = (char addr)
    over
    store
    
    ; Increment address
    push 1
    add
    
    jump :read_loop
    
:read_done
    drop  ; Drop CR
    drop  ; Drop addr
    return

; Parse RAM buffer and calculate
:parse_calc
    ; Read first number from RAM
    push 0
    call :parse_number
    ; Stack: num1 next_addr
    
    ; Load operator
    dup
    load
    ; Stack: num1 next_addr op
    
    >r      ; num1 next_addr, return: op
    
    ; Increment to next position
    push 1
    add
    
    ; Read second number
    call :parse_number
    ; Stack: num1 num2 next_addr
    
    drop    ; Don't need address anymore
    
    ; Stack: num1 num2
    r>      ; num1 num2 op
    
    ; Check operator
    dup
    push 43  ; '+'
    eq
    br_if :do_add
    
    dup
    push 45  ; '-'
    eq
    br_if :do_sub
    
    dup
    push 42  ; '*'
    eq
    br_if :do_mul
    
    dup
    push 47  ; '/'
    eq
    br_if :do_div
    
    ; Unknown operator
    drop
    drop
    drop
    jump :main

:do_add
    drop
    add
    call :print_number
    jump :main

:do_sub
    drop
    sub
    call :print_number
    jump :main

:do_mul
    drop
    mul
    call :print_number
    jump :main

:do_div
    drop
    ; Check for divide by zero
    dup
    eqz
    br_if :div_zero
    ; Perform division (a / b)
    swap
    dup
    >r      ; Save dividend
    swap
    dup
    >r      ; Save divisor
    
    ; Simple division by repeated subtraction
    push 0  ; quotient
    >r
    
:div_loop
    ; Stack: dividend divisor
    dup
    >r      ; Save divisor
    sub     ; dividend - divisor
    dup
    >r      ; Save result
    
    ; Check if result < 0 (signed)
    dup
    push 0
    lt_s
    br_if :div_done
    
    ; Increment quotient
    r>      ; Get result
    r>      ; Get divisor
    r>      ; Get quotient
    push 1
    add
    >r      ; Save quotient
    swap
    jump :div_loop
    
:div_done
    r>      ; Discard negative result
    r>      ; Discard divisor
    drop
    drop
    r>      ; Get quotient
    r>      ; Discard saved divisor
    drop
    r>      ; Discard saved dividend
    drop
    call :print_number
    jump :main

:div_zero
    drop
    drop
    push 69   ; 'E'
    print
    push 114  ; 'r'
    print
    push 114  ; 'r'
    print
    push 13
    print
    push 10
    print
    return

; Parse a decimal number from RAM
; Input: addr - starting address in RAM
; Output: number addr - parsed number and next address
:parse_number
    push 0  ; addr 0
    ; Stack: addr accumulator (accumulator on top)
    
:parse_loop
    ; Load character from RAM
    over    ; addr accum addr
    load    ; addr accum char
    ; Stack: addr accum char
    
    ; Check if it's a digit (0-9)
    dup
    push 48
    lt_s
    br_if :parse_done
    
    dup
    push 57
    gt_s
    br_if :parse_done
    
    ; It's a digit
    push 48
    sub     ; addr accum digit
    
    ; Accumulate: accum = accum * 10 + digit
    swap    ; addr digit accum
    push 10
    mul     ; addr digit (accum*10)
    add     ; addr new_accum
    
    ; Increment address
    swap    ; new_accum addr
    push 1
    add     ; new_accum (addr+1)
    swap    ; (addr+1) new_accum
    
    jump :parse_loop
    
:parse_done
    ; Stack: addr accum char
    drop    ; addr accum
    swap    ; accum addr
    return

; Print a decimal number
; Input: number on stack
:print_number
    dup
    eqz
    br_if :print_zero
    
    dup
    push 0
    lt_s
    br_if :print_negative
    
    ; Positive number
    call :print_unsigned
    push 13
    print
    push 10
    print
    return

:print_zero
    drop
    push 48  ; '0'
    print
    push 13
    print
    push 10
    print
    return

:print_negative
    ; Print minus sign
    push 45  ; '-'
    print
    
    ; Negate and print
    push 0
    swap
    sub
    call :print_unsigned
    push 13
    print
    push 10
    print
    return

; Print unsigned number by extracting digits
; Uses return stack to reverse digit order
:print_unsigned
    dup
    eqz
    br_if :print_u_done
    
:print_u_loop
    dup
    eqz
    br_if :print_u_output
    
    ; Divide by 10: quotient and remainder
    dup
    push 10
    
    ; Do division by repeated subtraction
    >r      ; Save dividend
    >r      ; Save 10
    push 0  ; quotient
    r>      ; Get 10
    r>      ; Get dividend
    
:divmod_loop
    dup
    push 10
    lt_s
    br_if :divmod_done
    
    push 10
    sub
    >r
    swap
    push 1
    add
    swap
    r>
    jump :divmod_loop
    
:divmod_done
    ; Stack: quotient remainder
    push 48
    add     ; Convert remainder to ASCII
    >r      ; Save digit on return stack
    
    ; Continue with quotient
    jump :print_u_loop

:print_u_output
    drop
    
:print_u_output_loop
    rdepth
    eqz
    br_if :print_u_done
    
    r>
    print
    jump :print_u_output_loop

:print_u_done
    return
