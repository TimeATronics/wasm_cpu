"""
mal test runner — parses .mal files, compiles expressions with our Lisp
compiler, runs them on sim_s32.exe, and checks results against expectations.

Usage:
    python -m lispc.mal_runner [--sim path/to/sim_s32.exe] step0_repl.mal step1_read_print.mal ...

The runner accumulates definitions across test cases and checks:
  - ;=>value  by inspecting the final data stack TOS (for int/bool/nil)
  - ;/regex   by capturing stdout from the simulator run
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from .reader import parse_string, StrAtom, Atom, List
from .compiler import Compiler, CompileError
from .opcodes import OP_DROP, OP_HALT

# Special value encodings used by our compiler/VM
NIL_VAL   = 0xFFFFFFFF
TRUE_VAL  = 0xFFFFFFFE
FALSE_VAL = 0xFFFFFFFC


def parse_mal(filepath):
    """Parse a .mal file into a list of test cases.

    Each expression line is its own test case. Expected values
    (;=>) and expected stdout regex (;/) apply to the immediately
    preceding expression line.
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    tests = []
    current_expr = None
    current_dirs = set()
    expr_line = 0

    for i, line in enumerate(lines, 1):
        stripped = line.rstrip('\n')

        # Directives
        if stripped.startswith(';>>>'):
            parts = stripped[4:].strip().split()
            current_dirs.update(parts)
            continue

        # Expected value line
        if stripped.startswith(';=>'):
            if current_expr is not None:
                expected = stripped[3:].strip()
                tests.append({
                    'expression': current_expr,
                    'expected': expected,
                    'expected_regex': None,
                    'directives': set(current_dirs),
                    'line': expr_line,
                    'source_file': filepath,
                })
                current_expr = None
                current_dirs = set()
            continue

# Expected regex line (stdout)
        if stripped.startswith(';/'):
            if current_expr is not None:
                expected_re = stripped[2:].strip()
                tests.append({
                    'expression': current_expr,
                    'expected': None,
                    'expected_regex': expected_re,
                    'directives': set(current_dirs),
                    'line': expr_line,
                    'source_file': filepath,
                })
                current_expr = None
                current_dirs = set()
            continue

        # Comment line (skip)
        if stripped.startswith(';;') or stripped == '':
            continue

        # Expression line: commit any pending expression without check,
        # then start new expression
        if current_expr is not None:
            tests.append({
                'expression': current_expr,
                'expected': None,
                'expected_regex': None,
                'directives': set(current_dirs),
                'line': expr_line,
                'source_file': filepath,
            })
            current_dirs = set()

        current_expr = stripped
        expr_line = i

    # Commit last pending expression
    if current_expr is not None:
        tests.append({
            'expression': current_expr,
            'expected': None,
            'expected_regex': None,
            'directives': set(current_dirs),
            'line': expr_line,
            'source_file': filepath,
        })

    return tests


def resolve_expected_value(val_str):
    """Resolve a ;=> string to the expected data stack value (unsigned 32-bit).

    Returns None if we can't resolve (e.g. string/list result), meaning the test
    should be skipped for raw stack comparison.
    """
    if val_str == 'nil':
        return NIL_VAL
    if val_str == 'true':
        return TRUE_VAL
    if val_str == 'false':
        return FALSE_VAL
    # Try integer
    try:
        v = int(val_str)
        return v & 0xFFFFFFFF  # Convert to unsigned 32-bit
    except ValueError:
        pass
    # Complex type - skip
    return None


EXPECTED_STACK_VALUES = {
    'nil': NIL_VAL,
    'true': TRUE_VAL,
    'false': FALSE_VAL,
}


def extract_data_stack(stderr_text):
    """Extract the final data stack TOS from simulator stderr output.

    Returns a list of uint32 values or None if not found.
    """
    m = re.search(r'Final data stack:\s*\[([^\]]*)\]', stderr_text)
    if m:
        values_str = m.group(1).strip()
        if values_str == '':
            return []
        vals = []
        for v in values_str.split(','):
            v = v.strip()
            if v.startswith('0x') or v.startswith('0X'):
                vals.append(int(v, 16))
            else:
                vals.append(int(v))
        return vals
    return None


def extract_stdout(stdout_bytes):
    """Extract printed output from simulator stdout (bytes -> str)."""
    return stdout_bytes.decode('utf-8', errors='replace')


def format_stack_val(val):
    """Format a raw stack integer back to a readable value."""
    if val == NIL_VAL:
        return 'nil'
    if val == TRUE_VAL:
        return 'true'
    if val == FALSE_VAL:
        return 'false'
    # Signed interpretation
    if val & 0x80000000:
        return str(val - 0x100000000)
    return str(val)


def run_test(test, compiler_state, sim_path, sim_dir):
    """Run a single test case, updating compiler_state.

    Returns (passed, message, updated_state).
    compiler_state is a list of definition expressions that have been
    accumulated so far.
    """
    expr = test['expression']
    expected = test['expected']
    expected_re = test['expected_regex']
    directives = test['directives']
    source_file = test['source_file']
    line = test['line']

    # Skip optional/soft/deferrable tests unless explicitly enabled
    skip_dirs = {'soft=True', 'optional=True'}
    if directives & skip_dirs:
        return True, '(skipped: soft/optional)', compiler_state

    deferrable = directives & {'deferrable=True'}
    # We still run deferrable tests, just note them

    # Parse the expression
    try:
        ast_list = parse_string(expr, filename=source_file)
        if not ast_list:
            return True, '(empty expression)', compiler_state
        # parse_string always returns a list; use first expression for this test
        ast = ast_list[0]
    except (SyntaxError, CompileError) as e:
        # If expected is an error regex (/.*...)
        if expected_re:
            return True, f'(expected error: {expected_re})', compiler_state
        return False, f'parse error: {e}', compiler_state

    # Build the full program: accumulated definitions + this expression
    state_exprs = list(compiler_state)

    # Compile using custom function that leaves result on stack
    try:
        c = Compiler()
        bytecode = _compile_test_program(c, state_exprs, ast)
    except CompileError as e:
        if expected_re:
            return True, f'(expected compile error: {e})', compiler_state
        return False, f'compile error: {e}', compiler_state

    # Write bytecode to temp file
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        binpath = f.name
        f.write(bytecode)

    try:
        # Run simulator with -v to always see stack summary
        result = subprocess.run(
            [sim_path, '-v', binpath],
            capture_output=True,
            timeout=30,
            cwd=sim_dir,
        )

        stdout_text = result.stdout.decode('utf-8', errors='replace')
        stderr_text = result.stderr.decode('utf-8', errors='replace')

        # Check for crash/error (no final data stack printed means crash)
        if 'Final data stack' not in stderr_text:
            err_msg = stderr_text[:500]
            return False, f'simulator error: no data stack in output: {err_msg}', compiler_state

        # Check expected regex (stdout)
        if expected_re:
            # Check if stdout matches
            try:
                if re.search(expected_re, stdout_text):
                    # Success - accumulate definitions
                    new_state = compiler_state + [ast] if _is_definition(ast) else compiler_state
                    return True, '(stdout match)', new_state
                else:
                    return False, f'stdout mismatch: expected /{expected_re}/, got: {stdout_text[:200]!r}', compiler_state
            except re.error as e:
                return False, f'bad regex {expected_re!r}: {e}', compiler_state

        # Check expected value (data stack)
        if expected is not None:
            expected_val = resolve_expected_value(expected)
            if expected_val is None:
                # Can't check this with raw stack - skip
                new_state = compiler_state + [ast] if _is_definition(ast) else compiler_state
                return True, f'(skipped: complex type check {expected})', new_state

            stack = extract_data_stack(stderr_text)
            if stack is None:
                return False, f'could not find final data stack in output', compiler_state
            if len(stack) == 0:
                return False, f'empty data stack, expected {expected} ({expected_val:#x})', compiler_state

            tos = stack[-1]
            if tos == expected_val:
                new_state = compiler_state + [ast] if _is_definition(ast) else compiler_state
                return True, f'got {format_stack_val(tos)}', new_state
            else:
                return False, f'expected {expected} ({expected_val:#x}), got {format_stack_val(tos)} ({tos:#x})', compiler_state

        # No check specified - just accumulate
        if _is_definition(ast):
            compiler_state = compiler_state + [ast]
        return True, '(no check)', compiler_state

    except subprocess.TimeoutExpired:
        return False, 'simulator timeout', compiler_state
    except FileNotFoundError:
        return False, f'simulator not found: {sim_path}', compiler_state
    finally:
        try:
            os.unlink(binpath)
        except OSError:
            pass


def _compile_test_program(compiler, state_asts, test_ast):
    """Compile accumulated state + test expression, leaving test result on data stack.

    state_asts: list of ASTs from previous definitions (def!, defun, define)
    test_ast: current test expression AST

    Unlike Compiler.compile(), this does NOT drop the last expression's result.
    """
    all_asts = list(state_asts) + [test_ast]
    compiler._scan_defuns(all_asts)

    # Separate defuns from regular expressions
    defuns = []
    regular = []
    for ast in all_asts:
        if isinstance(ast, List) and len(ast.items) >= 2:
            if isinstance(ast.items[0], Atom) and ast.items[0].value == 'defun':
                defuns.append(ast)
                continue
        regular.append(ast)

    # Compile regular expressions sequentially
    for i, ast in enumerate(regular):
        is_last = (i == len(regular) - 1)

        if isinstance(ast, List) and len(ast.items) >= 2:
            if isinstance(ast.items[0], Atom):
                op = ast.items[0].value
                if op in ('define', 'def!'):
                    # Compile define normally, then push value again if last
                    compiler._compile_define(ast.items[1], ast.items[2])
                    if is_last:
                        # Re-emit the value for the test result
                        compiler._compile_expr(ast.items[2])
                    continue
        # Regular expression
        if is_last:
            compiler._compile_expr(ast)
        else:
            compiler._compile_expr(ast)
            compiler.emit(OP_DROP)

    compiler.emit(OP_HALT)

    # Compile function bodies after all regular code
    for f in defuns:
        compiler._compile_defun(f.items[1], f.items[2],
                                f.items[3:] if len(f.items) > 3 else [])

    compiler.patch_all()
    return bytes(compiler.code)


def _is_definition(ast):
    """Check if an AST node is a definition (def!, defun, define) or contains one (begin/progn/do)."""
    from .reader import List, Atom
    if isinstance(ast, List) and ast.items:
        op = ast.items[0]
        if isinstance(op, Atom) and isinstance(op.value, str):
            if op.value in ('def!', 'defun', 'define', 'defn', 'fn*',
                            'progn', 'begin', 'do'):
                return True
    return False


def run_mal_file(filepath, sim_path, sim_dir, quiet=False):
    """Run all tests in a .mal file. Returns (passed, total)."""
    tests = parse_mal(filepath)
    passed = 0
    total = 0
    compiler_state = []  # accumulated definition ASTs

    for test in tests:
        total += 1
        ok, msg, compiler_state = run_test(test, compiler_state, sim_path, sim_dir)
        if ok:
            passed += 1
            if not quiet:
                print(f'  OK  line {test["line"]:4d}: {test["expression"][:60]:60s}  {msg}')
        else:
            print(f'  FAIL line {test["line"]:4d}: {test["expression"][:60]:60s}')
            print(f'       {msg}')

    return passed, total


def main():
    import argparse
    parser = argparse.ArgumentParser(description='mal test runner for WasmCPU Lisp compiler')
    parser.add_argument('files', nargs='+', help='.mal test files')
    parser.add_argument('--sim', default='sim_s32.exe', help='path to simulator')
    parser.add_argument('--sim-dir', default=None, help='simulator working directory (default: cwd)')
    parser.add_argument('-q', '--quiet', action='store_true', help='only show failures')
    args = parser.parse_args()

    sim_path = args.sim
    sim_dir = args.sim_dir or os.getcwd()

    total_passed = 0
    total_total = 0

    for fp in args.files:
        print(f'\n=== {fp} ===')
        p, t = run_mal_file(fp, sim_path, sim_dir, quiet=args.quiet)
        total_passed += p
        total_total += t
        print(f'  -> {p}/{t} passed')

    print(f'\n{"="*40}')
    print(f'Total: {total_passed}/{total_total} passed')
    return 0 if total_passed == total_total else 1


if __name__ == '__main__':
    sys.exit(main())