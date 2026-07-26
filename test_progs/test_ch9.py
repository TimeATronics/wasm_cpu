import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).parent.parent
S32_CC = ROOT / 's32-cc' / 's32-cc.exe'
SIM = ROOT / 'sim_s32.exe'
TESTS_DIR = ROOT / 'refs' / 'writing-a-c-compiler-tests' / 'tests'
EXPECTED = ROOT / 'refs' / 'writing-a-c-compiler-tests' / 'expected_results.json'
with open(EXPECTED) as f:
    ER = json.load(f)
passed = failed = timeout = 0
for source in sorted((TESTS_DIR / 'chapter_9' / 'valid').rglob('*.c')):
    key = source.relative_to(TESTS_DIR).as_posix()
    if key not in ER:
        continue
    expected = ER[key]
    with tempfile.TemporaryDirectory() as tmp:
        bin_path = Path(tmp) / (source.stem + '.bin')
        r = subprocess.run([str(S32_CC), '-o', str(bin_path), str(source)], capture_output=True, text=True, timeout=5)
        if r.returncode != 0:
            print(f'COMPILE_ERR {source.name}: {r.stderr.strip()[:80]}')
            continue
        try:
            r2 = subprocess.run([str(SIM), str(bin_path)], capture_output=True, text=True, timeout=30)
            ec = r2.returncode & 0xFF
            ok = ec == expected['return_code'] and r2.stdout == expected.get('stdout', '')
            if ok:
                passed += 1
            else:
                failed += 1
                print(f'FAIL {source.name}: exp={expected["return_code"]} got={ec}')
        except subprocess.TimeoutExpired:
            timeout += 1
            print(f'TIMEOUT {source.name}')
print(f'=== Ch9: {passed} passed, {failed} failed, {timeout} timeout ===')
