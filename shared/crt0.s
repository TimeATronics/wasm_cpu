; C runtime startup for WASM-S32
; Sets up the execution environment and calls main()

    .globl _start

_start:
    push 0          ; placeholder - linker patches this with total binary size
    >r              ; push frame_size for CALL convention
    call :main      ; CALL main() - pops frame_size, advances fp
    halt            ; halt after main returns
