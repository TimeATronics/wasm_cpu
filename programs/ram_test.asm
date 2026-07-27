; Test RAM store and load

:main
    ; Store 'A' at address 0
    push 65  ; 'A'
    push 0
    store
    
    ; Store 'B' at address 1
    push 66  ; 'B'
    push 1
    store
    
    ; Store 'C' at address 2
    push 67  ; 'C'
    push 2
    store
    
    ; Now read them back
    push 0
    load
    print    ; Should print 'A'
    
    push 1
    load
    print    ; Should print 'B'
    
    push 2
    load
    print    ; Should print 'C'
    
    push 13
    print
    push 10
    print
    
    halt
