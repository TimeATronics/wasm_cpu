#!/usr/bin/env python3
"""Compare sim.py output against sim_s32 for all programs."""
import subprocess, tempfile, os, sys, io

sys.path.insert(0, os.path.dirname(__file__))
from sim import StackCPU

def assemble(filename):
    src = os.path.join('programs', filename)
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        out = f.name.replace('.bin', '')
    try:
        r = subprocess.run(['node', 'scripts/assembler.js', src, out],
                          capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"Assembler: {r.stderr}")
        with open(out + '.bin', 'rb') as bf:
            return bf.read()
    finally:
        for ext in ['.bin', '.hex', '.vh']:
            p = out + ext
            if os.path.exists(p): os.remove(p)

def run_py(bc, input_data=""):
    cpu = StackCPU(bc)
    if input_data:
        old = sys.stdin
        sys.stdin = io.StringIO(input_data)
        try: cpu.run(max_steps=50000)
        finally: sys.stdin = old
    else:
        cpu.run(max_steps=50000)
    return ''.join(cpu.output)

def run_c(bc_file, input_data=""):
    """Run C simulator, capture stdout."""
    # Write binary to temp file
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(bc)
        tmp = f.name
    try:
        # On Windows, pipe input via PowerShell
        r = subprocess.run(
            [f'.\\sim_s32.exe', tmp],
            input=input_data.encode('latin-1') if input_data else None,
            capture_output=True, timeout=30
        )
        return r.stdout.decode('latin-1')
    finally:
        os.remove(tmp)

passed = 0
failed = 0

def check(name, py_out, c_out):
    global passed, failed
    if py_out == c_out:
        passed += 1
        print(f"  MATCH: {name}")
    else:
        failed += 1
        print(f"  MISMATCH: {name}")
        print(f"    py: {repr(py_out)}")
        print(f"    c:  {repr(c_out)}")

# Test simple programs
for name, inp in [
    ('hello_test.asm', ''),
    ('swap_test.asm', ''),
    ('ram_test.asm', ''),
    ('full_calc.asm', '5+3\r'),
    ('full_calc.asm', '9-4\r'),
    ('full_calc.asm', '3*7\r'),
    ('full_calc.asm', '12+34\r'),
    ('full_calc.asm', '7-3\r'),
    ('full_calc.asm', '3-8\r'),
]:
    short = os.path.basename(name).replace('.asm', '')
    label = f"{short}({repr(inp)})" if inp else short
    bc = assemble(name)
    py_out = run_py(bc, inp)
    c_out = run_c(bc, inp)
    check(label, py_out, c_out)

print(f"\n{'='*40}")
print(f"RESULTS: {passed} matched, {failed} mismatched")
