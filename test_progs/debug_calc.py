#!/usr/bin/env python3
"""Standalone debugger for full_calc.asm - runs with trace, limited steps."""
import sys, os, io, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
from sim import StackCPU


def assemble_file(filename):
    assembler = os.path.join(os.path.dirname(__file__), 'scripts', 'assembler.js')
    src_path = os.path.join(os.path.dirname(__file__), 'programs', filename)
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        out_base = f.name.replace('.bin', '')
    try:
        result = subprocess.run(
            ['node', assembler, src_path, out_base],
            capture_output=True, text=True
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


def run_with_trace(input_data, max_steps=500, trace_first_n=None):
    """Run calculator with optional trace of first N steps."""
    bc = assemble_file('full_calc.asm')
    cpu = StackCPU(bc)

    old_stdin = sys.stdin
    sys.stdin = io.StringIO(input_data)
    try:
        steps = 0
        while max_steps == 0 or steps < max_steps:
            if not cpu.step():
                break
            steps += 1
            if trace_first_n and steps <= trace_first_n:
                # step() already printed trace if enabled
                pass
    except RuntimeError as e:
        print(f"\n*** CPU ERROR at step {steps}: {e}")
        print(f"Stack: {[hex(x) for x in cpu.stack]}")
        print(f"RStack: {[hex(x) for x in cpu.rstack]}")
        print(f"Output so far: {repr(''.join(cpu.output))}")
    finally:
        sys.stdin = old_stdin

    output = ''.join(cpu.output)
    print(f"\nSteps: {cpu.steps}, Halted: {cpu.halted}")
    print(f"Output: {repr(output)}")
    return cpu


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'trace'

    if mode == 'trace':
        print("=== Tracing first 200 steps of '5+3' ===")
        cpu = StackCPU.__new__(StackCPU)
        bc = assemble_file('full_calc.asm')
        cpu.program = list(bc)
        cpu.pc = 0
        cpu.stack = []
        cpu.rstack = []
        cpu.ram = [0] * 1024
        cpu.halted = False
        cpu.output = []
        cpu.steps = 0

        old_stdin = sys.stdin
        sys.stdin = io.StringIO('5+3\r')
        cpu.trace = True
        try:
            while cpu.steps < 200:
                if not cpu.step():
                    break
                cpu.steps += 1
        except RuntimeError as e:
            print(f"\n*** CPU ERROR at step {cpu.steps}: {e}")
            print(f"Stack: {[hex(x) for x in cpu.stack]}")
            print(f"RStack: {[hex(x) for x in cpu.rstack]}")
        finally:
            sys.stdin = old_stdin
        print(f"\nOutput: {repr(''.join(cpu.output))}")

    elif mode == 'quick':
        print("=== Quick test '5+3' ===")
        run_with_trace('5+3\r', max_steps=500)

    elif mode == 'multi':
        for expr in ['5+3\r', '9-4\r', '3*7\r', '12+34\r', '7-3\r', '3-8\r']:
            print(f"\n{'='*40}")
            print(f"Testing: {repr(expr)}")
            run_with_trace(expr, max_steps=500)
