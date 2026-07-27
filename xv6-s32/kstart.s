; Kernel startup stub - jumps past init code to _start
.globl _entry
_entry:
    jump _start

; Fallback halt
    halt
