#!/usr/bin/env python3
"""
Simple simulator for the stack CPU to debug programs
"""

class StackCPU:
    def __init__(self, program):
        self.program = program
        self.pc = 0
        self.stack = []
        self.rstack = []
        self.halted = False
        self.output = []
        
    def push(self, val):
        self.stack.append(val & 0xFFFFFFFF)
        
    def pop(self):
        if not self.stack:
            raise Exception(f"Stack underflow at PC={self.pc}")
        return self.stack.pop()
    
    def rpush(self, val):
        self.rstack.append(val & 0xFFFFFFFF)
        
    def rpop(self):
        if not self.rstack:
            raise Exception(f"Return stack underflow at PC={self.pc}")
        return self.rstack.pop()
    
    def read_imm32(self):
        val = 0
        for i in range(4):
            val |= self.program[self.pc] << (i * 8)
            self.pc += 1
        return val
    
    def step(self):
        if self.halted or self.pc >= len(self.program):
            return False
            
        opcode = self.program[self.pc]
        self.pc += 1
        
        # Print state before execution
        print(f"PC={self.pc-1:3d} OP=0x{opcode:02x} Stack={self.stack} RStack={self.rstack}")
        
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
            print(f"  -> PRINT: '{ch}' ({val & 0xFF})")
        elif opcode == 0x09:  # EQ
            b = self.pop()
            a = self.pop()
            self.push(1 if a == b else 0)
        elif opcode == 0x0A:  # LT_S
            b = self.pop()
            a = self.pop()
            # Convert to signed
            a_signed = a if a < 0x80000000 else a - 0x100000000
            b_signed = b if b < 0x80000000 else b - 0x100000000
            self.push(1 if a_signed < b_signed else 0)
        elif opcode == 0x0E:  # BR_IF
            target = self.read_imm32()
            cond = self.pop()
            if cond != 0:
                self.pc = target
        elif opcode == 0x0F:  # JUMP
            target = self.read_imm32()
            self.pc = target
        elif opcode == 0x12:  # DUP
            val = self.stack[-1]
            self.push(val)
        elif opcode == 0x13:  # SWAP
            b = self.pop()
            a = self.pop()
            self.push(b)
            self.push(a)
        elif opcode == 0x14:  # OVER
            if len(self.stack) < 2:
                raise Exception(f"OVER needs at least 2 items on stack at PC={self.pc-1}")
            val = self.stack[-2]
            self.push(val)
        elif opcode == 0x30:  # >R (to return stack)
            val = self.pop()
            self.rpush(val)
        elif opcode == 0x31:  # R> (from return stack)
            val = self.rpop()
            self.push(val)
        elif opcode == 0x32:  # R@ (copy from return stack)
            if not self.rstack:
                raise Exception(f"R@ on empty return stack at PC={self.pc-1}")
            self.push(self.rstack[-1])
        elif opcode == 0x33:  # DEPTH
            self.push(len(self.stack))
        elif opcode == 0x34:  # RDEPTH
            self.push(len(self.rstack))
        elif opcode == 0xFF:  # HALT
            self.halted = True
            return False
        else:
            print(f"Unknown opcode 0x{opcode:02x} at PC={self.pc-1}")
            return False
            
        return True
    
    def run(self, max_steps=1000):
        steps = 0
        while steps < max_steps and self.step():
            steps += 1
        
        print(f"\n=== Execution finished after {steps} steps ===")
        print(f"Output: {''.join(self.output)}")
        print(f"Final stack: {self.stack}")
        print(f"Final return stack: {self.rstack}")

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: python sim.py <program.bin>")
        sys.exit(1)
    
    with open(sys.argv[1], 'rb') as f:
        program = list(f.read())
    
    cpu = StackCPU(program)
    cpu.run()
