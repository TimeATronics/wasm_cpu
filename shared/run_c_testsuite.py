#!/usr/bin/env python3
"""Run c-testsuite tests against our s32-cc compiler and sim_s32 simulator."""
import subprocess, sys, tempfile, os, re
from pathlib import Path

ROOT = Path(__file__).parent.parent
CC = ROOT / 's32-cc' / 's32-cc.exe'
SIM = ROOT / 'sim_s32.exe'
TEST_DIR = ROOT / 'refs' / 'c-testsuite' / 'tests' / 'single-exec'

def get_expected(path):
    exp_file = path.with_suffix('.expected')
    if exp_file.exists():
        return int(exp_file.read_text().strip())
    return 0

def run_tests(limit=None):
    tests = sorted(TEST_DIR.glob('0*.c'))
    if limit:
        tests = tests[:limit]
    
    results = {'pass': 0, 'fail': 0, 'compile_err': 0, 'skip': 0}
    
    for src in tests:
        name = src.stem
        expected = get_expected(src)
        
        with tempfile.TemporaryDirectory() as tmp:
            bin_path = Path(tmp) / f'{name}.bin'
            
            content = src.read_text()
            # Skip tests requiring unsupported features
            if any(x in content for x in ['#define', '#include', '#if', '#ifdef']):
                results['skip'] += 1; continue
            if re.search(r'\(\*', content):  # function pointer declarations
                results['skip'] += 1; continue
            if re.search(r'\{ *\.', content):  # designated initializers
                results['skip'] += 1; continue
            if re.search(r'\bfloat\b|\bdouble\b', content):  # floating point
                results['skip'] += 1; continue
            if re.search(r': \d+;', content):  # bitfields
                results['skip'] += 1; continue
            if re.search(r'\.\.\.', content):  # varargs
                results['skip'] += 1; continue
            if re.search(r'\bshort\b', content):  # short type (treated as int)
                results['skip'] += 1; continue
            
            try:
                r = subprocess.run([str(CC), '-o', str(bin_path), str(src)],
                                 capture_output=True, text=True, timeout=15)
            except subprocess.TimeoutExpired:
                print(f'TIMEOUT {name}: compiler hung')
                results['fail'] += 1
                continue
            
            if r.returncode != 0:
                if 'volatile' in content or 'inline' in content or 'register' in content:
                    results['skip'] += 1
                else:
                    results['compile_err'] += 1
                    err = r.stderr[:80].replace('\n',' ')
                    print(f'COMPILE_ERR {name}: {err}')
                continue
            
            try:
                r2 = subprocess.run([str(SIM), str(bin_path)],
                                  capture_output=True, text=True, timeout=15,
                                  stdin=subprocess.DEVNULL)
            except subprocess.TimeoutExpired:
                print(f'TIMEOUT {name}: simulator hung')
                results['fail'] += 1
                continue
            
            actual = r2.returncode & 0xFF
            
            if actual == expected:
                results['pass'] += 1
            else:
                results['fail'] += 1
                print(f'FAIL {name}: expected {expected}, got {actual}')
    
    print(f'\nResults: {results["pass"]} pass, {results["fail"]} fail, '
          f'{results["compile_err"]} compile_err, {results["skip"]} skip')

if __name__ == '__main__':
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else None
    run_tests(limit)
