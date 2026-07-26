#!/usr/bin/env python3
"""
WASM-S32 Reference Simulator
Full instruction set simulation for the stack CPU architecture.
"""

import sys
import struct

# Memory size (words). Matches the 10-bit address space (1024 words = 4 KB)
RAM_WORDS = 1024

# Opcode table for disassembly
OPCODE_NAMES = {
    0x01: 'push', 0x02: 'add', 0x03: 'sub', 0x04: 'mul',
    0x05: 'drop', 0x08: 'print',
    0x09: 'eq', 0x0A: 'lt_s', 0x0B: 'gt_s', 0x0C: 'lt_u', 0x0D: 'gt_u',
    0x0E: 'br_if', 0x0F: 'jump', 0x10: 'call', 0x11: 'return',
    0x12: 'dup', 0x13: 'swap', 0x14: 'over', 0x15: 'rot',
    0x16: 'and', 0x17: 'or', 0x18: 'xor', 0x19: 'not',
    0x1A: 'shl', 0x1B: 'shr_u', 0x1C: 'shr_s',
    0x1D: 'load', 0x1E: 'store', 0x1F: 'key',
    0x30: '>r', 0x31: 'r>', 0x32: 'r@',
    0x33: 'depth', 0x34: 'rdepth', 0x35: 'eqz',
    0x36: 'div_s', 0x37: 'load8_u', 0x38: 'store8',
    0x39: 'local.get', 0x3A: 'local.set',
    0x3B: 'sysenter', 0x3C: 'eret',
    0x3D: 'csr_read', 0x3E: 'csr_write',
    0x3F: 'tlb_flush',
    0xFF: 'halt',
}

# CSR register IDs
CSR_STATUS = 0
CSR_SATP   = 1
CSR_EVEC   = 2
CSR_EPC    = 3
CSR_EDATA  = 4

# Privilege modes
MODE_USER   = 0
MODE_KERNEL = 1


def to_signed32(val):
    """Convert unsigned 32-bit value to signed."""
    val = val & 0xFFFFFFFF
    return val if val < 0x80000000 else val - 0x100000000


def to_unsigned32(val):
    """Convert signed value to unsigned 32-bit."""
    return val & 0xFFFFFFFF


class StackCPU:
    def __init__(self, program):
        self.program = list(program)
        self.pc = 0
        self.stack = []       # Data stack (unlimited in simulation)
        self.rstack = []      # Return stack (unlimited in simulation)
        self.ram = [0] * RAM_WORDS  # Data memory (word-addressed)
        self.halted = False
        self.output = []      # Captured print output
        self.trace = False    # Enable instruction tracing
        self.steps = 0
        # CSR registers (Phase 1)
        self.csr = [0] * 8    # STATUS, SATP, EVEC, EPC, EDATA, ...
        self.privilege = MODE_KERNEL  # Start in kernel mode

    def push(self, val):
        self.stack.append(to_unsigned32(val))

    def pop(self):
        if not self.stack:
            raise RuntimeError(f"Data stack underflow at PC=0x{self.pc:04X}")
        return self.stack.pop()

    def rpush(self, val):
        self.rstack.append(to_unsigned32(val))

    def rpop(self):
        if not self.rstack:
            raise RuntimeError(f"Return stack underflow at PC=0x{self.pc:04X}")
        return self.rstack.pop()

    def peek(self, offset=0):
        """Peek at stack item without popping. offset=0 is top."""
        idx = len(self.stack) - 1 - offset
        if idx < 0:
            raise RuntimeError(f"Stack peek underflow at PC=0x{self.pc:04X}")
        return self.stack[idx]

    def read_imm32(self):
        val = 0
        for i in range(4):
            if self.pc + i >= len(self.program):
                raise RuntimeError(f"PC out of bounds reading imm32 at 0x{self.pc:04X}")
            val |= self.program[self.pc + i] << (i * 8)
        self.pc += 4
        return val

    def read_imm8(self):
        if self.pc >= len(self.program):
            raise RuntimeError(f"PC out of bounds reading imm8 at 0x{self.pc:04X}")
        val = self.program[self.pc]
        self.pc += 1
        return val

    def ram_read(self, addr):
        # Hardware uses word addressing: addr 0 = word 0, addr 1 = word 1, etc.
        word_idx = addr & (RAM_WORDS - 1)
        return self.ram[word_idx]

    def ram_write(self, addr, val):
        word_idx = addr & (RAM_WORDS - 1)
        self.ram[word_idx] = to_unsigned32(val)

    def step(self):
        if self.halted or self.pc >= len(self.program):
            return False

        start_pc = self.pc
        opcode = self.program[self.pc]
        self.pc += 1

        name = OPCODE_NAMES.get(opcode, f'???')

        if self.trace:
            print(f"  PC=0x{start_pc:04X} {name:12s}  stack=[{', '.join(f'0x{x:08X}' for x in self.stack)}]  rstack=[{', '.join(f'0x{x:08X}' for x in self.rstack)}]")

        if opcode == 0x01:  # PUSH
            val = self.read_imm32()
            self.push(val)

        elif opcode == 0x02:  # ADD
            b = self.pop()
            a = self.pop()
            self.push(a + b)

        elif opcode == 0x03:  # SUB
            b = self.pop()
            a = self.pop()
            self.push(a - b)

        elif opcode == 0x04:  # MUL
            b = self.pop()
            a = self.pop()
            self.push(a * b)

        elif opcode == 0x05:  # DROP
            self.pop()

        elif opcode == 0x08:  # PRINT
            val = self.pop()
            ch = chr(val & 0xFF)
            self.output.append(ch)

        elif opcode == 0x09:  # EQ
            b = self.pop()
            a = self.pop()
            self.push(1 if a == b else 0)

        elif opcode == 0x0A:  # LT_S
            b = self.pop()
            a = self.pop()
            self.push(1 if to_signed32(a) < to_signed32(b) else 0)

        elif opcode == 0x0B:  # GT_S
            b = self.pop()
            a = self.pop()
            self.push(1 if to_signed32(a) > to_signed32(b) else 0)

        elif opcode == 0x0C:  # LT_U
            b = self.pop()
            a = self.pop()
            self.push(1 if a < b else 0)

        elif opcode == 0x0D:  # GT_U
            b = self.pop()
            a = self.pop()
            self.push(1 if a > b else 0)

        elif opcode == 0x0E:  # BR_IF
            target = self.read_imm32()
            cond = self.pop()
            if cond != 0:
                self.pc = target

        elif opcode == 0x0F:  # JUMP
            target = self.read_imm32()
            self.pc = target

        elif opcode == 0x10:  # CALL
            target = self.read_imm32()
            self.rpush(self.pc)
            self.pc = target

        elif opcode == 0x11:  # RETURN
            self.pc = self.rpop()

        elif opcode == 0x12:  # DUP
            val = self.peek(0)
            self.push(val)

        elif opcode == 0x13:  # SWAP
            b = self.pop()
            a = self.pop()
            self.push(b)
            self.push(a)

        elif opcode == 0x14:  # OVER
            val = self.peek(1)
            self.push(val)

        elif opcode == 0x15:  # ROT
            if len(self.stack) < 3:
                raise RuntimeError(f"ROT needs 3 items on stack at PC=0x{start_pc:04X}")
            c = self.pop()
            b = self.pop()
            a = self.pop()
            self.push(b)
            self.push(c)
            self.push(a)

        elif opcode == 0x16:  # AND
            b = self.pop()
            a = self.pop()
            self.push(a & b)

        elif opcode == 0x17:  # OR
            b = self.pop()
            a = self.pop()
            self.push(a | b)

        elif opcode == 0x18:  # XOR
            b = self.pop()
            a = self.pop()
            self.push(a ^ b)

        elif opcode == 0x19:  # NOT
            a = self.pop()
            self.push(~a)

        elif opcode == 0x1A:  # SHL
            b = self.pop()
            a = self.pop()
            self.push(a << (b & 0x1F))

        elif opcode == 0x1B:  # SHR_U
            b = self.pop()
            a = self.pop()
            self.push(a >> (b & 0x1F))

        elif opcode == 0x1C:  # SHR_S
            b = self.pop()
            a = self.pop()
            shift = b & 0x1F
            self.push(to_unsigned32(to_signed32(a) >> shift))

        elif opcode == 0x1D:  # LOAD
            addr = self.pop()
            val = self.ram_read(addr)
            self.push(val)

        elif opcode == 0x1E:  # STORE
            addr = self.pop()
            val = self.pop()
            self.ram_write(addr, val)

        elif opcode == 0x1F:  # KEY
            # In simulation, read from stdin
            try:
                ch = sys.stdin.read(1)
                if ch:
                    self.push(ord(ch))
                else:
                    self.push(0)  # EOF
            except EOFError:
                self.push(0)

        elif opcode == 0x30:  # >R (to return stack)
            val = self.pop()
            self.rpush(val)

        elif opcode == 0x31:  # R> (from return stack)
            val = self.rpop()
            self.push(val)

        elif opcode == 0x32:  # R@ (copy return stack top)
            if not self.rstack:
                raise RuntimeError(f"R@ on empty return stack at PC=0x{start_pc:04X}")
            self.push(self.rstack[-1])

        elif opcode == 0x33:  # DEPTH
            self.push(len(self.stack))

        elif opcode == 0x34:  # RDEPTH
            self.push(len(self.rstack))

        elif opcode == 0x35:  # EQZ
            a = self.pop()
            self.push(1 if a == 0 else 0)

        elif opcode == 0x36:  # DIV_S
            b = self.pop()
            a = self.pop()
            if b != 0:
                # Truncate toward zero (C semantics), not floor (Python semantics)
                self.push(to_unsigned32(int(to_signed32(a) / to_signed32(b))))
            else:
                self.push(0)  # Division by zero returns 0

        elif opcode == 0x37:  # LOAD8_U
            addr = self.pop()
            word_idx = (addr >> 2) & (RAM_WORDS - 1)
            byte_idx = addr & 0x3
            word = self.ram[word_idx]
            byte_val = (word >> (byte_idx * 8)) & 0xFF
            self.push(byte_val)

        elif opcode == 0x38:  # STORE8
            addr = self.pop()
            val = self.pop()
            word_idx = (addr >> 2) & (RAM_WORDS - 1)
            byte_idx = addr & 0x3
            old_word = self.ram[word_idx]
            mask = ~(0xFF << (byte_idx * 8)) & 0xFFFFFFFF
            new_word = (old_word & mask) | ((val & 0xFF) << (byte_idx * 8))
            self.ram[word_idx] = new_word

        elif opcode == 0x39:  # LOCAL_GET
            idx = self.read_imm8()
            val = self.ram_read(idx)
            self.push(val)

        elif opcode == 0x3A:  # LOCAL_SET
            idx = self.read_imm8()
            val = self.pop()
            self.ram_write(idx, val)

        elif opcode == 0x3B:  # SYSENTER
            # Trap into kernel mode
            self.csr[CSR_EPC] = start_pc
            self.csr[CSR_EDATA] = 0  # syscall number passed on stack
            self.csr[CSR_STATUS] |= (MODE_KERNEL << 0)  # set KM bit
            self.csr[CSR_STATUS] &= ~(MODE_USER << 1)   # clear UM bit
            self.privilege = MODE_KERNEL
            # Jump to EVEC + 0x10 (syscall vector)
            if self.csr[CSR_EVEC]:
                self.pc = self.csr[CSR_EVEC] + 0x10

        elif opcode == 0x3C:  # ERET
            # Return from exception/interrupt
            self.csr[CSR_STATUS] &= ~(MODE_KERNEL << 0)  # clear KM
            self.csr[CSR_STATUS] |= (MODE_USER << 1)     # set UM
            self.privilege = MODE_USER
            self.pc = self.csr[CSR_EPC]

        elif opcode == 0x3D:  # CSR_READ
            csr_id = self.read_imm32() & 0xFF
            if csr_id < len(self.csr):
                self.push(self.csr[csr_id])
            else:
                self.push(0)

        elif opcode == 0x3E:  # CSR_WRITE
            csr_id = self.read_imm32() & 0xFF
            val = self.pop()
            if csr_id < len(self.csr):
                self.csr[csr_id] = val

        elif opcode == 0x3F:  # TLB_FLUSH
            pass  # No-op in simulation (no TLB yet)

        elif opcode == 0xFF:  # HALT
            self.halted = True
            return False

        else:
            raise RuntimeError(f"Unknown opcode 0x{opcode:02X} at PC=0x{start_pc:04X}")

        return True

    def run(self, max_steps=0, verbose=False):
        """Run the program. max_steps=0 means unlimited."""
        self.trace = verbose
        self.steps = 0
        try:
            while max_steps == 0 or self.steps < max_steps:
                if not self.step():
                    break
                self.steps += 1
        except RuntimeError:
            raise

        if verbose:
            output_str = ''.join(self.output)
            print(f"\n=== Execution finished after {self.steps} steps ===")
            print(f"Output: {repr(output_str)}")
            if self.stack:
                print(f"Final data stack: {[f'0x{x:08X}' for x in self.stack]}")
            if self.rstack:
                print(f"Final return stack: {[f'0x{x:08X}' for x in self.rstack]}")
        elif self.output:
            output_str = ''.join(self.output)
            print(output_str, end='')

    def dump_ram(self, start_word=0, count=16):
        """Dump RAM contents."""
        print(f"\nRAM dump (word addresses {start_word}-{start_word+count-1}):")
        for i in range(count):
            addr = start_word + i
            if addr < RAM_WORDS:
                print(f"  [{addr:4d}] 0x{self.ram[addr]:08X}")


def main():
    import argparse
    parser = argparse.ArgumentParser(description='WASM-S32 Reference Simulator')
    parser.add_argument('program', help='Binary file to execute')
    parser.add_argument('-m', '--max-steps', type=int, default=0,
                        help='Maximum steps (0=unlimited)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable instruction tracing')
    parser.add_argument('-d', '--dump-ram', action='store_true',
                        help='Dump RAM after execution')
    parser.add_argument('-r', '--run-only', action='store_true',
                        help='Run silently, only show final output')
    args = parser.parse_args()

    with open(args.program, 'rb') as f:
        program = f.read()

    if not args.run_only:
        print(f"Loaded {len(program)} bytes from {args.program}")

    cpu = StackCPU(program)

    if args.run_only:
        cpu.trace = False
    elif args.verbose:
        cpu.trace = True

    cpu.run(max_steps=args.max_steps, verbose=args.verbose)

    if args.dump_ram:
        cpu.dump_ram()

    # Return top of data stack as exit code (like C's main returning int)
    if cpu.stack:
        sys.exit(cpu.stack[-1] & 0xFF)


if __name__ == "__main__":
    main()
