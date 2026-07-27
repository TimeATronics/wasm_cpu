; Test SWAP behavior precisely
; Stack starts: [1, 43, 1]

push 1
push 43  
push 1

; Print initial state
push 83  ; 'S'
print
push 58  ; ':'
print

depth
push 48
add
print    ; Should print '3'

push 32
print

; Print each element (top to bottom)
dup
push 48
add  
print    ; top = 1

swap
dup
push 48
add
print    ; was second = 43... will be wrong

swap
dup
push 48  
add
print    ; was third = 1

push 13
print
push 10
print

; Restore to [1, 43, 1]
drop
drop  
drop
push 1
push 43
push 1

; Now do the actual SWAP
push 65  ; 'A'
print
push 102 ; 'f'
print
push 116 ; 't'
print
push 101 ; 'e'
print
push 114 ; 'r'
print
push 32
print
push 115 ; 's'
print
push 119 ; 'w'
print
push 97  ; 'a'
print
push 112 ; 'p'
print
push 58  ; ':'
print
push 32
print

swap

; Print depth
depth
push 48
add
print

push 32
print

; Print elements
dup
push 48
add
print

push 32
print

swap
dup
push 48
add
print

push 32
print

swap
dup
push 48
add
print

push 13
print
push 10
print

halt
