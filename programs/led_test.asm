; LED blink test - known pattern
; Write 0x3F to MMIO LED address => led <= ~0x3F = 0x00 (all 6 LEDs ON)

:start
    push 0x3F        ; value: all 6 LEDs ON (led = ~v[5:0])
    push 0xFFFFF000  ; LED MMIO address
    store            ; write to LEDs

:loop
    jump :loop       ; infinite loop