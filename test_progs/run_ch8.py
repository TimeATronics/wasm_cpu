import json, subprocess, sys, tempfile, os
from pathlib import Path

ROOT = Path(__file__).parent.parent
S32_CC = ROOT / 's32-cc' / 's32-cc.exe'
SIM = ROOT / 'sim.py'
TESTS_DIR = ROOT / 'refs' / 'writing-a-c-compiler-tests' / 'tests'
EXPECTED = ROOT / 'refs' / 'writing-a-c-compiler-tests' / 'expected_results.json'
with open(EXPECTED) as f:
    EXPECTED_RESULTS = json.load(f)

passed = failed = timeout = 0
for source in sorted((TESTS_DIR / 'chapter_8' / 'valid').rglob('*.c')):
    key = source.relative_to(TESTS_DIR).as_posix()
    if key not in EXPECTED_RESULTS: continue
    expected = EXPECTED_RESULTS[key]
    with tempfile.TemporaryDirectory() as tmpdir:
        bin_path = Path(tmpdir) / (source.stem + '.bin')
        r = subprocess.run([str(S32_CC), '-o', str(bin_path), str(source)], capture_output=True, text=True, timeout=5)
        if r.returncode != 0:
            print(f'FAIL {source.stem}: PARSE ERROR')
            failed += 1
            continue
        try:
            r2 = subprocess.run([sys.executable, str(SIM), str(bin_path), '-r'], capture_output=True, text=True, timeout=5)
            exit_code = r2.returncode & 0xFF
            ok = exit_code == expected['return_code'] and r2.stdout == expected.get('stdout', '')
            status = 'PASS' if ok else 'FAIL'
            if not ok:
                print(f'{status} {source.stem}: exp={expected["return_code"]} got={exit_code}')
                failed += 1
            else:
                passed += 1
        except subprocess.TimeoutExpired:
            print(f'TIMEOUT {source.stem}')
            timeout += 1
print(f'\n=== Ch8: {passed} passed, {failed} failed, {timeout} timeout ===')
