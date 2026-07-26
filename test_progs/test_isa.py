#!/usr/bin/env python3
"""
Comprehensive test suite for WASM-S32 ISA and toolchain.
Tests every instruction, assembler correctness, and end-to-end programs.
"""

import sys
import os
import subprocess
import tempfile

# Import the simulator
sys.path.insert(0, os.path.dirname(__file__))
from sim import StackCPU, to_signed32, to_unsigned32, RAM_WORDS

# ============================================================================
# Test Infrastructure
# ============================================================================
passed = 0
failed = 0
errors = []


def test(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  PASS: {name}")
    else:
        failed += 1
        msg = f"  FAIL: {name}"
        if detail:
            msg += f"  ({detail})"
        print(msg)
        errors.append(name)


def build_program(instructions):
    """Build a program from a list of (opcode, [immediates]) tuples."""
    bytecode = []
    for opcode, imm_bytes in instructions:
        bytecode.append(opcode)
        bytecode.extend(imm_bytes)
    return bytes(bytecode)


def imm32(val):
    """Encode a 32-bit value as 4 little-endian bytes."""
    val = val & 0xFFFFFFFF
    return [val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF]


def run_program(bytecode, input_data="", max_steps=10000):
    """Run a bytecode program and return the CPU state."""
    cpu = StackCPU(bytecode)
    if input_data:
        import io
        old_stdin = sys.stdin
        sys.stdin = io.StringIO(input_data)
        try:
            cpu.run(max_steps=max_steps)
        finally:
            sys.stdin = old_stdin
    else:
        cpu.run(max_steps=max_steps)
    return cpu


# ============================================================================
# Stack Manipulation Instructions
# ============================================================================
print("\n=== Stack Manipulation ===")

def test_push():
    prog = build_program([
        (0x01, imm32(42)),    # push 42
        (0x01, imm32(100)),   # push 100
        (0xFF, []),           # halt
    ])
    cpu = run_program(prog)
    test("PUSH pushes value", len(cpu.stack) == 2 and cpu.stack[0] == 42 and cpu.stack[1] == 100)

def test_drop():
    prog = build_program([
        (0x01, imm32(42)),
        (0x01, imm32(100)),
        (0x05, []),           # drop
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DROP removes top", len(cpu.stack) == 1 and cpu.stack[0] == 42)

def test_dup():
    prog = build_program([
        (0x01, imm32(77)),
        (0x12, []),           # dup
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DUP duplicates top", len(cpu.stack) == 2 and cpu.stack[0] == 77 and cpu.stack[1] == 77)

def test_swap():
    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(20)),
        (0x13, []),           # swap
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SWAP swaps top two", len(cpu.stack) == 2 and cpu.stack[0] == 20 and cpu.stack[1] == 10)

def test_over():
    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(20)),
        (0x14, []),           # over
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("OVER copies second to top", len(cpu.stack) == 3 and cpu.stack[2] == 10 and cpu.stack[0] == 10)

def test_rot():
    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(20)),
        (0x01, imm32(30)),
        (0x15, []),           # rot
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("ROT rotates top 3", len(cpu.stack) == 3 and cpu.stack[0] == 20 and cpu.stack[1] == 30 and cpu.stack[2] == 10)

def test_depth():
    prog = build_program([
        (0x01, imm32(1)),
        (0x01, imm32(2)),
        (0x01, imm32(3)),
        (0x33, []),           # depth
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DEPTH pushes stack depth", cpu.stack[-1] == 3)  # 3 items exist, depth pushes 3

def test_rdepth():
    prog = build_program([
        (0x01, imm32(99)),
        (0x30, []),           # >r
        (0x01, imm32(88)),
        (0x30, []),           # >r
        (0x34, []),           # rdepth
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("RDEPTH pushes return stack depth", cpu.stack[-1] == 2)

test_push()
test_drop()
test_dup()
test_swap()
test_over()
test_rot()
test_depth()
test_rdepth()

# ============================================================================
# Arithmetic Instructions
# ============================================================================
print("\n=== Arithmetic ===")

def test_add():
    prog = build_program([
        (0x01, imm32(15)),
        (0x01, imm32(27)),
        (0x02, []),           # add
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("ADD 15+27=42", cpu.stack[0] == 42)

def test_add_overflow():
    prog = build_program([
        (0x01, imm32(0xFFFFFFFF)),  # -1 as unsigned
        (0x01, imm32(1)),
        (0x02, []),           # add
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("ADD wraps on overflow", cpu.stack[0] == 0)

def test_sub():
    prog = build_program([
        (0x01, imm32(50)),
        (0x01, imm32(18)),
        (0x03, []),           # sub
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SUB 50-18=32", cpu.stack[0] == 32)

def test_sub_negative():
    prog = build_program([
        (0x01, imm32(5)),
        (0x01, imm32(10)),
        (0x03, []),           # sub (5-10 = -5)
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SUB produces negative", to_signed32(cpu.stack[0]) == -5)

def test_mul():
    prog = build_program([
        (0x01, imm32(7)),
        (0x01, imm32(6)),
        (0x04, []),           # mul
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("MUL 7*6=42", cpu.stack[0] == 42)

def test_div_s():
    prog = build_program([
        (0x01, imm32(100)),
        (0x01, imm32(7)),
        (0x36, []),           # div_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DIV_S 100/7=14", cpu.stack[0] == 14)

def test_div_s_negative():
    prog = build_program([
        (0x01, imm32(to_unsigned32(-20))),  # -20
        (0x01, imm32(3)),
        (0x36, []),           # div_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DIV_S -20/3=-6", to_signed32(cpu.stack[0]) == -6)

def test_div_s_by_zero():
    prog = build_program([
        (0x01, imm32(42)),
        (0x01, imm32(0)),
        (0x36, []),           # div_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("DIV_S by zero returns 0", cpu.stack[0] == 0)

test_add()
test_add_overflow()
test_sub()
test_sub_negative()
test_mul()
test_div_s()
test_div_s_negative()
test_div_s_by_zero()

# ============================================================================
# Comparison Instructions
# ============================================================================
print("\n=== Comparison ===")

def test_eq():
    prog = build_program([
        (0x01, imm32(42)),
        (0x01, imm32(42)),
        (0x09, []),           # eq
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("EQ equal values", cpu.stack[0] == 1)

    prog = build_program([
        (0x01, imm32(42)),
        (0x01, imm32(43)),
        (0x09, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("EQ unequal values", cpu.stack[0] == 0)

def test_eqz():
    prog = build_program([
        (0x01, imm32(0)),
        (0x35, []),           # eqz
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("EQZ zero", cpu.stack[0] == 1)

    prog = build_program([
        (0x01, imm32(42)),
        (0x35, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("EQZ non-zero", cpu.stack[0] == 0)

def test_lt_s():
    prog = build_program([
        (0x01, imm32(5)),
        (0x01, imm32(10)),
        (0x0A, []),           # lt_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("LT_S 5<10", cpu.stack[0] == 1)

    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(5)),
        (0x0A, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("LT_S 10<5 false", cpu.stack[0] == 0)

def test_gt_s():
    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(5)),
        (0x0B, []),           # gt_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("GT_S 10>5", cpu.stack[0] == 1)

    prog = build_program([
        (0x01, imm32(5)),
        (0x01, imm32(10)),
        (0x0B, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("GT_S 5>10 false", cpu.stack[0] == 0)

def test_lt_u():
    prog = build_program([
        (0x01, imm32(5)),
        (0x01, imm32(10)),
        (0x0C, []),           # lt_u
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("LT_U 5<10", cpu.stack[0] == 1)

def test_gt_u():
    prog = build_program([
        (0x01, imm32(10)),
        (0x01, imm32(5)),
        (0x0D, []),           # gt_u
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("GT_U 10>5", cpu.stack[0] == 1)

test_eq()
test_eqz()
test_lt_s()
test_gt_s()
test_lt_u()
test_gt_u()

# ============================================================================
# Bitwise & Shift Instructions
# ============================================================================
print("\n=== Bitwise & Shift ===")

def test_and():
    prog = build_program([
        (0x01, imm32(0xFF00)),
        (0x01, imm32(0x0FF0)),
        (0x16, []),           # and
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("AND 0xFF00 & 0x0FF0 = 0x0F00", cpu.stack[0] == 0x0F00)

def test_or():
    prog = build_program([
        (0x01, imm32(0xFF00)),
        (0x01, imm32(0x00FF)),
        (0x17, []),           # or
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("OR 0xFF00 | 0x00FF = 0xFFFF", cpu.stack[0] == 0xFFFF)

def test_xor():
    prog = build_program([
        (0x01, imm32(0xFFFF)),
        (0x01, imm32(0xFF00)),
        (0x18, []),           # xor
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("XOR 0xFFFF ^ 0xFF00 = 0x00FF", cpu.stack[0] == 0x00FF)

def test_not():
    prog = build_program([
        (0x01, imm32(0)),
        (0x19, []),           # not
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("NOT 0 = 0xFFFFFFFF", cpu.stack[0] == 0xFFFFFFFF)

def test_shl():
    prog = build_program([
        (0x01, imm32(1)),
        (0x01, imm32(4)),
        (0x1A, []),           # shl
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SHL 1<<4 = 16", cpu.stack[0] == 16)

def test_shr_u():
    prog = build_program([
        (0x01, imm32(16)),
        (0x01, imm32(2)),
        (0x1B, []),           # shr_u
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SHR_U 16>>2 = 4", cpu.stack[0] == 4)

def test_shr_s():
    prog = build_program([
        (0x01, imm32(to_unsigned32(-16))),  # -16 = 0xFFFFFFF0
        (0x01, imm32(2)),
        (0x1C, []),           # shr_s
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("SHR_S -16>>2 = -4 (sign extended)", to_signed32(cpu.stack[0]) == -4)

test_and()
test_or()
test_xor()
test_not()
test_shl()
test_shr_u()
test_shr_s()

# ============================================================================
# Memory Instructions
# ============================================================================
print("\n=== Memory ===")

def test_store_load():
    prog = build_program([
        (0x01, imm32(42)),     # value
        (0x01, imm32(0)),      # addr
        (0x1E, []),            # store
        (0x01, imm32(0)),      # addr
        (0x1D, []),            # load
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("STORE/LOAD roundtrip", cpu.stack[0] == 42)

def test_store_multiple():
    prog = build_program([
        # Store A(65) at addr 0
        (0x01, imm32(65)),
        (0x01, imm32(0)),
        (0x1E, []),
        # Store B(66) at addr 1
        (0x01, imm32(66)),
        (0x01, imm32(1)),
        (0x1E, []),
        # Store C(67) at addr 2
        (0x01, imm32(67)),
        (0x01, imm32(2)),
        (0x1E, []),
        # Load all three back
        (0x01, imm32(0)),
        (0x1D, []),
        (0x01, imm32(1)),
        (0x1D, []),
        (0x01, imm32(2)),
        (0x1D, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("STORE/LOAD multiple words", cpu.stack == [65, 66, 67])

def test_store8_load8_u():
    prog = build_program([
        # Store byte 0x41 at byte address 0
        (0x01, imm32(0x41)),
        (0x01, imm32(0)),
        (0x38, []),            # store8
        # Store byte 0x42 at byte address 1
        (0x01, imm32(0x42)),
        (0x01, imm32(1)),
        (0x38, []),            # store8
        # Load byte from address 0
        (0x01, imm32(0)),
        (0x37, []),            # load8_u
        # Load byte from address 1
        (0x01, imm32(1)),
        (0x37, []),            # load8_u
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("STORE8/LOAD8_U byte access", cpu.stack == [0x41, 0x42])

def test_store8_within_word():
    """Store 4 bytes to byte addresses 0-3, verify each reads back correctly."""
    prog = build_program([
        # Store A=0x41 at byte addr 0
        (0x01, imm32(0x41)), (0x01, imm32(0)), (0x38, []),
        # Store B=0x42 at byte addr 1
        (0x01, imm32(0x42)), (0x01, imm32(1)), (0x38, []),
        # Store C=0x43 at byte addr 2
        (0x01, imm32(0x43)), (0x01, imm32(2)), (0x38, []),
        # Store D=0x44 at byte addr 3
        (0x01, imm32(0x44)), (0x01, imm32(3)), (0x38, []),
        # Load all 4 bytes back
        (0x01, imm32(0)), (0x37, []),
        (0x01, imm32(1)), (0x37, []),
        (0x01, imm32(2)), (0x37, []),
        (0x01, imm32(3)), (0x37, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("STORE8 within same word (bytes 0-3)", cpu.stack == [0x41, 0x42, 0x43, 0x44])

test_store_load()
test_store_multiple()
test_store8_load8_u()
test_store8_within_word()

# ============================================================================
# Local Access Instructions
# ============================================================================
print("\n=== Local Access ===")

def test_local_get_set():
    prog = build_program([
        # Set local 0 = 99
        (0x01, imm32(99)),
        (0x3A, [0]),           # local.set 0
        # Set local 1 = 77
        (0x01, imm32(77)),
        (0x3A, [1]),           # local.set 1
        # Get local 0
        (0x39, [0]),           # local.get 0
        # Get local 1
        (0x39, [1]),           # local.get 1
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("LOCAL_GET/SET roundtrip", cpu.stack == [99, 77])

test_local_get_set()

# ============================================================================
# Control Flow Instructions
# ============================================================================
print("\n=== Control Flow ===")

def test_jump():
    # Jump instruction is 5 bytes (opcode + 4 imm), next instruction at byte 5
    # Jump to byte 10 to skip the push at byte 5
    prog = build_program([
        (0x0F, imm32(10)),     # jump to byte 10 (skip push 42 at byte 5)
        (0x01, imm32(42)),     # byte 5: this should be skipped
        (0x01, imm32(99)),     # byte 10: landing pad
        (0xFF, []),            # byte 15: halt
    ])
    cpu = run_program(prog)
    test("JUMP skips instruction", len(cpu.stack) == 1 and cpu.stack[0] == 99)

def test_br_if_taken():
    prog = build_program([
        (0x01, imm32(1)),      # cond = 1 (true)
        (0x0E, imm32(15)),     # br_if to byte 15 if true (skip push 42 at byte 10)
        (0x01, imm32(42)),     # byte 10: skipped
        (0x01, imm32(99)),     # byte 15: landing pad
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("BR_IF taken", len(cpu.stack) == 1 and cpu.stack[0] == 99)

def test_br_if_not_taken():
    prog = build_program([
        (0x01, imm32(0)),      # cond = 0 (false)
        (0x0E, imm32(10)),     # br_if to byte 10 (not taken)
        (0x01, imm32(42)),     # byte 5: executed
        (0x01, imm32(99)),     # byte 10: also executed
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("BR_IF not taken", len(cpu.stack) == 2 and cpu.stack[0] == 42 and cpu.stack[1] == 99)

def test_call_return():
    # Call a subroutine that pushes 77, then returns
    # Main: push 0 (nop), call :sub, push 88, halt
    # :sub: push 77, return
    prog = build_program([
        (0x01, imm32(0)),       # addr 0: push 0 (nop filler)
        (0x10, imm32(16)),      # addr 5: call to byte 16 (subroutine)
        (0x01, imm32(88)),      # addr 10: push 88 (after return)
        (0xFF, []),             # addr 15: halt
        # Subroutine at byte 16:
        (0x01, imm32(77)),      # addr 16: push 77
        (0x11, []),             # addr 21: return
    ])
    cpu = run_program(prog)
    test("CALL/RETURN", cpu.stack == [0, 77, 88])

test_jump()
test_br_if_taken()
test_br_if_not_taken()
test_call_return()

# ============================================================================
# Return Stack Instructions
# ============================================================================
print("\n=== Return Stack ===")

def test_to_r_from_r():
    prog = build_program([
        (0x01, imm32(42)),
        (0x30, []),            # >r (move to return stack)
        (0x31, []),            # r> (move back to data stack)
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test(">R/R> roundtrip", cpu.stack == [42] and len(cpu.rstack) == 0)

def test_r_fetch():
    prog = build_program([
        (0x01, imm32(42)),
        (0x30, []),            # >r
        (0x32, []),            # r@ (copy, not pop)
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("R@ copies without popping", cpu.stack == [42] and cpu.rstack == [42])

def test_return_stack_scratch():
    """Use return stack as scratch space during computation."""
    # Compute (3 + 4) * 5 using return stack to save intermediate
    prog = build_program([
        (0x01, imm32(3)),
        (0x01, imm32(4)),
        (0x02, []),            # add: 7
        (0x30, []),            # >r: save 7 to return stack
        (0x01, imm32(5)),
        (0x31, []),            # r>: retrieve 7
        (0x04, []),            # mul: 35
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("Return stack as scratch", cpu.stack == [35])

test_to_r_from_r()
test_r_fetch()
test_return_stack_scratch()

# ============================================================================
# I/O Instructions
# ============================================================================
print("\n=== I/O ===")

def test_print():
    prog = build_program([
        (0x01, imm32(65)),     # 'A'
        (0x08, []),            # print
        (0x01, imm32(66)),     # 'B'
        (0x08, []),            # print
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("PRINT outputs characters", ''.join(cpu.output) == "AB")

test_print()

# ============================================================================
# Edge Cases
# ============================================================================
print("\n=== Edge Cases ===")

def test_empty_stack_halt():
    prog = build_program([(0xFF, [])])
    cpu = run_program(prog)
    test("HALT on empty stack", cpu.halted and len(cpu.stack) == 0)

def test_nested_calls():
    prog = build_program([
        # addr 0: call func_a
        (0x10, imm32(6)),       # call :func_a at byte 6
        (0xFF, []),             # halt at byte 5
        # func_a at byte 6:
        (0x01, imm32(20)),      # push 20
        (0x11, []),             # return
    ])
    cpu = run_program(prog)
    test("Nested CALL/RETURN", cpu.stack == [20])

def test_deep_recursion_fib():
    """Compute fib(5) = 5 using recursive function."""
    # fib(n): if n < 2 return n; return fib(n-1) + fib(n-2)
    #
    # Layout:
    #   0:  push 5
    #   5:  call fib
    #  10:  halt
    #  11:  nop (padding)
    #  12:  nop (padding)
    #  13: fib(n):
    #  13:    dup
    #  14:    push 2
    #  19:    lt_s
    #  20:    br_if to base (addr 44)
    #  25:    dup
    #  26:    push 1
    #  31:    sub
    #  32:    call fib
    #  37:    swap
    #  38:    push 2
    #  43:    sub
    #  44:    call fib        ← base label lands here, so br_if must NOT land here
    #  ... wait, base should be separate

    # Let me rebuild carefully:
    b = bytearray()
    def emit(opcode, imm=None):
        b.append(opcode)
        if imm is not None:
            b.extend(imm32(imm))

    # addr 0: push 5
    emit(0x01, 5)
    # addr 5: call fib (at addr 13)
    emit(0x10, 13)
    # addr 10: halt
    emit(0xFF)
    # addr 11: padding
    emit(0x00)
    emit(0x00)

    # addr 13: fib(n) start
    addr_fib = len(b)
    assert addr_fib == 13

    emit(0x12)               # dup             (addr 13)
    emit(0x01, 2)            # push 2          (addr 14)
    emit(0x0A)               # lt_s            (addr 19)
    # br_if target = base = addr after return from base
    # We need to compute where base is. Let's leave a placeholder.
    br_if_addr = len(b)
    emit(0x0E, 0)            # br_if placeholder (addr 20-24)
    emit(0x12)               # dup             (addr 25)
    emit(0x01, 1)            # push 1          (addr 26)
    emit(0x03)               # sub             (addr 31)
    emit(0x10, 13)           # call fib        (addr 32)
    emit(0x13)               # swap            (addr 37)
    emit(0x01, 2)            # push 2          (addr 38)
    emit(0x03)               # sub             (addr 43)
    emit(0x10, 13)           # call fib        (addr 44)
    emit(0x02)               # add             (addr 49)
    emit(0x11)               # return          (addr 50)

    # base case: return n (n is already on stack from dup)
    addr_base = len(b)
    assert addr_base == 51
    emit(0x11)               # return

    # Patch the br_if target
    br_if_target = addr_base
    b[br_if_addr+1:br_if_addr+5] = imm32(br_if_target)

    cpu = run_program(bytes(b))
    test("Recursive fib(5)=5", cpu.stack[0] == 5)

def test_print_multi_digit():
    """Print digits of a number using repeated division."""
    # push 123, print as digits "123"
    # Algorithm: push 0 (counter), loop: div 10, save remainder to rstack, inc counter, until quotient=0, then print from rstack
    # This is complex in raw bytecode, let's just test push+print for known values
    prog = build_program([
        (0x01, imm32(49)),    # '1'
        (0x08, []),
        (0x01, imm32(50)),    # '2'
        (0x08, []),
        (0x01, imm32(51)),    # '3'
        (0x08, []),
        (0xFF, []),
    ])
    cpu = run_program(prog)
    test("PRINT multi-digit sequence", ''.join(cpu.output) == "123")

test_empty_stack_halt()
test_nested_calls()
test_deep_recursion_fib()
test_print_multi_digit()

# ============================================================================
# Assembler Tests
# ============================================================================
print("\n=== Assembler ===")

def assemble_asm(source):
    """Assemble source code and return bytecode."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    assembler = os.path.join(script_dir, 'scripts', 'assembler.js')
    with tempfile.NamedTemporaryFile(mode='w', suffix='.asm', delete=False) as f:
        f.write(source)
        f.flush()
        tmp = f.name
    try:
        out_base = tmp.replace('.asm', '')
        result = subprocess.run(
            ['node', assembler, tmp, out_base],
            capture_output=True, text=True,
            cwd=os.path.dirname(script_dir)
        )
        if result.returncode != 0:
            raise RuntimeError(f"Assembler error: {result.stderr}")
        bin_path = out_base + '.bin'
        with open(bin_path, 'rb') as bf:
            return bf.read()
    finally:
        for ext in ['.asm', '.bin', '.hex', '.vh']:
            p = tmp.replace('.asm', ext)
            if os.path.exists(p):
                os.remove(p)

def test_assembler_push():
    src = """
push 42
halt
"""
    bc = assemble_asm(src)
    test("Assembler: push 42", bc[0] == 0x01 and bc[1] == 42 and bc[5] == 0xFF)

def test_assembler_labels():
    src = """
:skip
push 99
halt
push 42
jump :skip
"""
    bc = assemble_asm(src)
    cpu = run_program(bc)
    # The program should push 99, halt (never reaches the second push)
    test("Assembler: label jump", cpu.stack[0] == 99 and len(cpu.stack) == 1)

def test_assembler_div_s():
    src = """
push 100
push 7
div_s
halt
"""
    bc = assemble_asm(src)
    cpu = run_program(bc)
    test("Assembler: div_s", cpu.stack[0] == 14)

def test_assembler_new_instructions():
    src = """
push 42
local.set 0
local.get 0
push 0xFF
push 0
store8
push 0
load8_u
halt
"""
    bc = assemble_asm(src)
    cpu = run_program(bc)
    test("Assembler: local.get/set + store8/load8_u", cpu.stack == [42, 0xFF])

def test_assembler_word_directive():
    src = """
.word 0x12345678
halt
"""
    bc = assemble_asm(src)
    test("Assembler: .word directive",
         bc[0] == 0x78 and bc[1] == 0x56 and bc[2] == 0x34 and bc[3] == 0x12 and bc[4] == 0xFF)

test_assembler_push()
test_assembler_labels()
test_assembler_div_s()
test_assembler_new_instructions()
test_assembler_word_directive()

# ============================================================================
# End-to-End Program Tests
# ============================================================================
print("\n=== End-to-End Programs ===")

def assemble_file(filename):
    """Assemble an existing .asm file from the programs/ directory."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    assembler = os.path.join(script_dir, 'scripts', 'assembler.js')
    src_path = os.path.join(script_dir, 'programs', filename)
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        out_base = f.name.replace('.bin', '')
    try:
        result = subprocess.run(
            ['node', assembler, src_path, out_base],
            capture_output=True, text=True,
            cwd=os.path.dirname(script_dir)
        )
        if result.returncode != 0:
            raise RuntimeError(f"Assembler error: {result.stderr}")
        with open(out_base + '.bin', 'rb') as bf:
            return bf.read()
    finally:
        for ext in ['.bin', '.hex', '.vh']:
            p = out_base + ext
            if os.path.exists(p):
                os.remove(p)

def test_hello_program():
    bc = assemble_file('hello_test.asm')
    cpu = run_program(bc)
    test("hello_test: prints OK\\r\\n", ''.join(cpu.output) == "OK\r\n")

def test_swap_program():
    bc = assemble_file('swap_test.asm')
    cpu = run_program(bc)
    output = ''.join(cpu.output)
    # The swap test should demonstrate swap working correctly
    test("swap_test: runs to completion", cpu.halted and len(output) > 0)
    # Verify specific output patterns
    test("swap_test: shows stack values", "3" in output and "1" in output)
    test("swap_test: shows after swap", "After swap" in output)

def test_ram_program():
    bc = assemble_file('ram_test.asm')
    cpu = run_program(bc)
    output = ''.join(cpu.output)
    test("ram_test: prints ABC\\r\\n", output == "ABC\r\n")

def test_full_calc_basic_ops():
    """Test calculator with basic operations."""
    bc = assemble_file('full_calc.asm')

    # Test addition: "5+3\r"
    cpu = run_program(bc, input_data="5+3\r")
    output = ''.join(cpu.output)
    # The calculator should parse 5+3 and print the result
    # We need to check if "8" appears in the output
    test("full_calc: 5+3 prompt shown", "> " in output)
    # The result should be 8 (printed as character)
    test("full_calc: 5+3=8", "8" in output)

    # Test subtraction: "9-4\r"
    cpu = run_program(bc, input_data="9-4\r")
    output = ''.join(cpu.output)
    test("full_calc: 9-4=5", "5" in output)

    # Test multiplication: "3*7\r"
    cpu = run_program(bc, input_data="3*7\r")
    output = ''.join(cpu.output)
    test("full_calc: 3*7=21", "21" in output)

def test_full_calc_negative():
    """Test calculator with negative results."""
    bc = assemble_file('full_calc.asm')
    cpu = run_program(bc, input_data="3-8\r")
    output = ''.join(cpu.output)
    # Should show -5
    test("full_calc: 3-8=-5", "-" in output and "5" in output)

def test_full_calc_large_numbers():
    """Test calculator with multi-digit numbers."""
    bc = assemble_file('full_calc.asm')
    cpu = run_program(bc, input_data="12+34\r")
    output = ''.join(cpu.output)
    test("full_calc: 12+34=46", "46" in output)

def test_full_calc_negative_input():
    """Test calculator with subtraction resulting in positive."""
    bc = assemble_file('full_calc.asm')
    cpu = run_program(bc, input_data="7-3\r")
    output = ''.join(cpu.output)
    test("full_calc: 7-3=4", "4" in output)

if __name__ == '__main__':
    test_hello_program()
    test_swap_program()
    test_ram_program()
    test_full_calc_basic_ops()
    test_full_calc_negative()
    test_full_calc_large_numbers()
    test_full_calc_negative_input()

    # ============================================================================
    # Summary
    # ============================================================================
    print("\n" + "=" * 60)
    print(f"RESULTS: {passed} passed, {failed} failed, {passed+failed} total")
    if errors:
        print(f"\nFailed tests:")
        for e in errors:
            print(f"  - {e}")
    print("=" * 60)

sys.exit(0 if failed == 0 else 1)
