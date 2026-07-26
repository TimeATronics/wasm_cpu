# WASM-S32 Instruction Set Architecture

**Version:** 1.0
**Date:** 2026-07-25
**Status:** Stable (Phase 0)

---

## 1. Overview

WASM-S32 is a 32-bit stack-based instruction set architecture inspired by WebAssembly. It is designed for simplicity, compact code density, and ease of hardware implementation on resource-constrained FPGAs.

### Key Characteristics
- **Word size:** 32 bits
- **Stack depth:** Hardware-limited (8 entries default), unlimited in simulation
- **Instruction encoding:** Variable-length (1 or 5 bytes: 1-byte opcode + optional 4-byte little-endian immediate)
- **Memory:** Byte-addressable data memory, word-addressed in hardware (10-bit address = 4 KB)
- **Endianness:** Little-endian for immediate values
- **Arithmetic:** Two's complement signed integers (unsigned where noted)

---

## 2. Architectural State

### 2.1 Data Stack
- Hardware: 8 entries x 32-bit (register-based distributed RAM)
- Simulator: Unlimited depth (Python list)
- Accessed via push/pop operations
- Top of stack = `stack[sp-1]` in hardware, `stack[-1]` in simulator

### 2.2 Return Stack
- Hardware: 8 entries x 32-bit
- Simulator: Unlimited depth
- Used for subroutine return addresses and general scratch storage (Forth-style `>r`/`r>`)

### 2.3 Program Counter (PC)
- 24-bit byte address (hardware: SPI flash addressing)
- Simulator: 32-bit index into program byte array

### 2.4 Frame Pointer (FP)
- 32-bit word address
- Used by `local.get`/`local.set` for frame-relative variable access
- Set during function prologue

---

## 3. Instruction Encoding

### Format
```
[opcode: 1 byte] [optional immediate: 4 bytes, little-endian]
```

Total instruction size: 1 byte (no immediate) or 5 bytes (with immediate).

### Immediate Types
| Type | Size | Description |
|------|------|-------------|
| imm32 | 4 bytes | 32-bit constant or address (little-endian) |
| idx8 | 1 byte | Local variable index (0-255) |

---

## 4. Complete Instruction Set

### 4.1 Stack Manipulation

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x01 | `push` | imm32 | `( -- val )` | Push 32-bit constant onto data stack |
| 0x05 | `drop` | None | `( a -- )` | Discard top element |
| 0x12 | `dup` | None | `( a -- a a )` | Duplicate top element |
| 0x13 | `swap` | None | `( a b -- b a )` | Swap top two elements |
| 0x14 | `over` | None | `( a b -- a b a )` | Copy second element to top |
| 0x15 | `rot` | None | `( a b c -- b c a )` | Rotate top three elements |
| 0x33 | `depth` | None | `( -- n )` | Push data stack depth |
| 0x34 | `rdepth` | None | `( -- n )` | Push return stack depth |

### 4.2 Arithmetic

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x02 | `add` | None | `( a b -- a+b )` | Signed integer addition |
| 0x03 | `sub` | None | `( a b -- a-b )` | Signed integer subtraction |
| 0x04 | `mul` | None | `( a b -- a*b )` | Signed integer multiplication |
| 0x36 | `div_s` | None | `( a b -- a/b )` | Signed integer division (div-by-zero returns 0) |

### 4.3 Comparison

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x09 | `eq` | None | `( a b -- cond )` | Push 1 if a == b, else 0 |
| 0x0A | `lt_s` | None | `( a b -- cond )` | Push 1 if a < b (signed), else 0 |
| 0x0B | `gt_s` | None | `( a b -- cond )` | Push 1 if a > b (signed), else 0 |
| 0x0C | `lt_u` | None | `( a b -- cond )` | Push 1 if a < b (unsigned), else 0 |
| 0x0D | `gt_u` | None | `( a b -- cond )` | Push 1 if a > b (unsigned), else 0 |
| 0x35 | `eqz` | None | `( a -- cond )` | Push 1 if a == 0, else 0 |

### 4.4 Bitwise & Shift

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x16 | `and` | None | `( a b -- a&b )` | Bitwise AND |
| 0x17 | `or` | None | `( a b -- a\|b )` | Bitwise OR |
| 0x18 | `xor` | None | `( a b -- a^b )` | Bitwise XOR |
| 0x19 | `not` | None | `( a -- ~a )` | Bitwise NOT (complement) |
| 0x1A | `shl` | None | `( a b -- a<<b )` | Logical shift left (b mod 32) |
| 0x1B | `shr_u` | None | `( a b -- a>>b )` | Logical shift right (unsigned, b mod 32) |
| 0x1C | `shr_s` | None | `( a b -- a>>>b )` | Arithmetic shift right (signed, b mod 32) |

### 4.5 Memory Access

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x1D | `load` | None | `( addr -- val )` | Load 32-bit word from RAM[addr/4] |
| 0x1E | `store` | None | `( val addr -- )` | Store 32-bit word to RAM[addr/4] |
| 0x37 | `load8_u` | None | `( addr -- val )` | Load zero-extended byte from RAM |
| 0x38 | `store8` | None | `( val addr -- )` | Store byte to RAM (read-modify-write) |

**Memory addressing:** `addr` is a byte address. Hardware divides by 4 (word address) and uses low 2 bits for byte selection within the word.

### 4.6 Frame Access

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x39 | `local.get` | idx8 | `( -- val )` | Push word from RAM[idx*4] |
| 0x3A | `local.set` | idx8 | `( val -- )` | Store word to RAM[idx*4] |

**Note:** In the current implementation, `local.get`/`local.set` use the index as a direct word address. A proper frame-pointer-relative scheme will be implemented for the C compiler.

### 4.7 Control Flow

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x0E | `br_if` | imm32 | `( cond -- )` | Pop cond; jump to addr if cond != 0 |
| 0x0F | `jump` | imm32 | `( -- )` | Unconditional jump to address |
| 0x10 | `call` | imm32 | `( -- )` | Push return address to return stack, jump |
| 0x11 | `return` | None | `( -- )` | Pop return address from return stack, jump |

### 4.8 Return Stack Operations

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x30 | `>r` | None | `( a -- ) (R: -- a )` | Move top of data stack to return stack |
| 0x31 | `r>` | None | `( -- a ) (R: a -- )` | Move top of return stack to data stack |
| 0x32 | `r@` | None | `( -- a ) (R: a -- a )` | Copy top of return stack to data stack |

### 4.9 I/O

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0x08 | `print` | None | `( a -- )` | Pop top, send low byte as character via UART TX |
| 0x1F | `key` | None | `( -- char )` | Block until character received via UART RX, push it |

### 4.10 System

| Opcode | Mnemonic | Immediate | Stack Effect | Description |
|--------|----------|-----------|--------------|-------------|
| 0xFF | `halt` | None | `( -- )` | Stop CPU execution |

### 4.11 Future / Reserved (Phase 1+)

These opcodes are allocated but not yet implemented in hardware:

| Opcode | Mnemonic | Immediate | Stack Effect | Privilege | Description |
|--------|----------|-----------|--------------|-----------|-------------|
| 0x3B | `sysenter` | None | `( sys_num -- )` | User | Trap into kernel mode |
| 0x3C | `eret` | None | `( -- )` | Kernel | Return from exception/interrupt |
| 0x3D | `csr_read` | csr_id16 | `( -- val )` | Kernel | Read CSR to stack |
| 0x3E | `csr_write` | csr_id16 | `( val -- )` | Kernel | Write stack top to CSR |
| 0x3F | `tlb_flush` | None | `( -- )` | Kernel | Flush TLB |

---

## 5. Memory Model

### 5.1 Hardware Memory Map
```
Code Space:   External SPI Flash (24-bit byte address, up to 16 MB)
Data Space:   Internal Block RAM (10-bit word address, 1024 words = 4 KB)
Stack Space:  Register-based (8 entries x 32-bit, separate data/return stacks)
```

### 5.2 Simulator Memory
```
Program:      Byte array loaded from .bin file
RAM:          1024-word array (4 KB), word-addressed
Data Stack:   Unlimited Python list
Return Stack: Unlimited Python list
```

### 5.3 Byte Ordering
- All immediate values: **Little-endian** (LSB first)
- RAM word storage: Native host endianness (simulator-dependent)

---

## 6. Instruction Cycle (Hardware)

Each instruction goes through a multi-cycle FSM:

| Cycle | State | Action |
|-------|-------|--------|
| 1 | FETCH | Initiate flash read at PC |
| 2-3 | FETCH_WAIT | Wait for flash data ready |
| 4 | DECODE | Capture opcode, increment PC, check if immediate needed |
| 5-16 | FETCH_IMM | Fetch 0 or 4 immediate bytes (3 cycles each) |
| 17 | EXECUTE | Perform operation |

**Cycle counts by instruction type:**
- Simple ALU (ADD, SUB, MUL, etc.): ~5 cycles
- PUSH with 4-byte immediate: ~17 cycles
- SWAP: ~6 cycles (extra cycle for temp register)
- LOAD/STORE: ~7 cycles (extra cycle for RAM access)
- PRINT/KEY: Variable (UART timing dependent)

---

## 7. Assembler Syntax

### Instructions
```asm
push 42          ; Push decimal value
push 0xFF        ; Push hex value
add              ; No immediate
br_if :label     ; Branch to label
jump :label      ; Jump to label
call :func       ; Call function
local.get 0      ; Get local variable at index 0
local.set 1      ; Set local variable at index 1
```

### Labels
```asm
:label_name      ; Define a label (byte offset)
:label_name      ; Reference in br_if/jump/call operands
```

### Directives
```asm
.word 0x12345678 ; Embed raw 32-bit value
.byte 0xFF       ; Embed raw 8-bit value
```

### Comments
```asm
; This is a comment (semicolons to end of line)
```

### Assembler Usage
```bash
node scripts/assembler.js programs/example.asm output
# Produces: output.hex, output.vh, output.bin
```

---

## 8. Example Programs

### 8.1 Hello World
```asm
; Print "OK\r\n"
push 79     ; 'O'
print
push 75     ; 'K'
print
push 13     ; '\r'
print
push 10     ; '\n'
print
halt
```

### 8.2 Fibonacci
```asm
; fib(n) - recursive Fibonacci
; Assumes n is on stack, result left on stack

:fib
    dup
    push 2
    lt_s
    br_if :fib_base

    dup
    push 1
    sub
    call :fib     ; fib(n-1)

    swap
    push 2
    sub
    call :fib     ; fib(n-2)

    add
    return

:fib_base
    return        ; n < 2, return n

; Main: compute fib(10)
:main
    push 10
    call :fib
    ; Result (55) is on stack
    halt
```

### 8.3 Factorial (with division)
```asm
; fact(n) - recursive factorial

:fact
    dup
    push 1
    gt_s
    br_if :fact_recurse
    ; Base case: n <= 1, return 1
    drop
    push 1
    return

:fact_recurse
    dup
    push 1
    sub
    call :fact
    mul
    return

:main
    push 6
    call :fact     ; fact(6) = 720
    halt
```

---

## 9. Known Limitations

1. **Stack depth:** Hardware limited to 8 entries (configurable via `STACK_DEPTH_LOG2` parameter)
2. **No hardware multiply-accumulate:** MUL and ADD are separate instructions
3. **No floating-point:** Integer-only arithmetic
4. **No atomic operations:** No lock-free primitives
5. **No hardware interrupts:** Currently polled I/O (Phase 1 adds timer/interrupt support)
6. **No privilege modes:** Single execution mode (Phase 1 adds Kernel/User modes)
7. **No MMU:** Direct physical addressing only (Phase 1 adds page tables)
8. **RAM is word-addressed:** Byte access requires read-modify-write via `load8_u`/`store8`

---

## 10. Future Extensions (Phase 1+)

See `DESIGN_REFINED.MD` for the complete roadmap. Key ISA additions planned:

- **Privilege modes:** Kernel (Ring 0) and User (Ring 3)
- **CSRs:** STATUS, SATP, EVEC, EPC, EDATA
- **Trap/interrupt handling:** Hardware-automated context save
- **MMU:** 2-level page tables, 4 KB pages
- **Timer:** 32-bit free-running counter with compare interrupt
- **Additional memory instructions:** Byte-granular atomic access
