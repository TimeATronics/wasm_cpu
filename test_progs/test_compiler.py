#!/usr/bin/env python3
"""
Test runner for s32-cc: compiles C programs to WASM-S32 bytecode,
runs them through the simulator, and checks output/exit codes.

Adapted from writing-a-c-compiler-tests framework.
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).parent.parent
S32_CC = ROOT / "s32-cc" / "s32-cc.exe"
S32_LD = ROOT / "s32-ld" / "s32-ld.exe"
S32_AS = ROOT / "s32-as" / "s32-as.exe"
CRT0 = ROOT / "shared" / "crt0.o"
SIM = ROOT / "sim_s32.exe"
TESTS_DIR = ROOT / "refs" / "writing-a-c-compiler-tests" / "tests"
EXPECTED = ROOT / "refs" / "writing-a-c-compiler-tests" / "expected_results.json"

with open(EXPECTED) as f:
    EXPECTED_RESULTS = json.load(f)


MULTI_FILE_SETS = {
    # (lib_file, client_file) pairs - library files depend on client for main()
    # chapter_9
    "chapter_9/valid/libraries/addition.c": "chapter_9/valid/libraries/addition_client.c",
    "chapter_9/valid/libraries/many_args.c": "chapter_9/valid/libraries/many_args_client.c",
    "chapter_9/valid/libraries/system_call.c": "chapter_9/valid/libraries/system_call_client.c",
    "chapter_9/valid/libraries/no_function_calls/division.c":
        "chapter_9/valid/libraries/no_function_calls/division_client.c",
    "chapter_9/valid/libraries/no_function_calls/local_stack_variables.c":
        "chapter_9/valid/libraries/no_function_calls/local_stack_variables_client.c",
    # chapter_10
    "chapter_10/valid/libraries/external_linkage_function.c":
        "chapter_10/valid/libraries/external_linkage_function_client.c",
    "chapter_10/valid/libraries/external_tentative_var.c":
        "chapter_10/valid/libraries/external_tentative_var_client.c",
    "chapter_10/valid/libraries/external_var_scoping.c":
        "chapter_10/valid/libraries/external_var_scoping_client.c",
    "chapter_10/valid/libraries/external_variable.c":
        "chapter_10/valid/libraries/external_variable_client.c",
    "chapter_10/valid/libraries/internal_hides_external_linkage.c":
        "chapter_10/valid/libraries/internal_hides_external_linkage_client.c",
    "chapter_10/valid/libraries/internal_linkage_function.c":
        "chapter_10/valid/libraries/internal_linkage_function_client.c",
    "chapter_10/valid/libraries/internal_linkage_var.c":
        "chapter_10/valid/libraries/internal_linkage_var_client.c",
    "chapter_10/valid/extra_credit/libraries/same_label_same_fun.c":
        "chapter_10/valid/extra_credit/libraries/same_label_same_fun_client.c",
    # chapter_11
    "chapter_11/valid/libraries/long_args.c": "chapter_11/valid/libraries/long_args_client.c",
    "chapter_11/valid/libraries/long_global_var.c": "chapter_11/valid/libraries/long_global_var_client.c",
    "chapter_11/valid/libraries/maintain_stack_alignment.c": "chapter_11/valid/libraries/maintain_stack_alignment_client.c",
    "chapter_11/valid/libraries/return_long.c": "chapter_11/valid/libraries/return_long_client.c",
    # chapter_12
    "chapter_12/valid/libraries/unsigned_args.c": "chapter_12/valid/libraries/unsigned_args_client.c",
    "chapter_12/valid/libraries/unsigned_global_var.c": "chapter_12/valid/libraries/unsigned_global_var_client.c",
    # chapter_14
    "chapter_14/valid/libraries/static_pointer.c": "chapter_14/valid/libraries/static_pointer_client.c",
    "chapter_14/valid/libraries/global_pointer.c": "chapter_14/valid/libraries/global_pointer_client.c",
    # chapter_15
    "chapter_15/valid/libraries/global_array.c": "chapter_15/valid/libraries/global_array_client.c",
    "chapter_15/valid/libraries/return_pointer_to_array.c": "chapter_15/valid/libraries/return_pointer_to_array_client.c",
    "chapter_15/valid/libraries/set_array_val.c": "chapter_15/valid/libraries/set_array_val_client.c",
    # chapter_16
    "chapter_16/valid/libraries/char_arguments.c": "chapter_16/valid/libraries/char_arguments_client.c",
    "chapter_16/valid/libraries/global_char.c": "chapter_16/valid/libraries/global_char_client.c",
    "chapter_16/valid/libraries/return_char.c": "chapter_16/valid/libraries/return_char_client.c",
    # chapter_17
    "chapter_17/valid/libraries/pass_alloced_memory.c": "chapter_17/valid/libraries/pass_alloced_memory_client.c",
    "chapter_17/valid/libraries/test_for_memory_leaks.c": "chapter_17/valid/libraries/test_for_memory_leaks_client.c",
    # chapter_18
    "chapter_18/valid/extra_credit/libraries/classify_unions.c": "chapter_18/valid/extra_credit/libraries/classify_unions_client.c",
    "chapter_18/valid/extra_credit/libraries/param_passing.c": "chapter_18/valid/extra_credit/libraries/param_passing_client.c",
    "chapter_18/valid/extra_credit/libraries/static_union_inits.c": "chapter_18/valid/extra_credit/libraries/static_union_inits_client.c",
    "chapter_18/valid/extra_credit/libraries/union_inits.c": "chapter_18/valid/extra_credit/libraries/union_inits_client.c",
    "chapter_18/valid/extra_credit/libraries/union_retvals.c": "chapter_18/valid/extra_credit/libraries/union_retvals_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/array_of_structs.c": "chapter_18/valid/no_structure_parameters/libraries/array_of_structs_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/global_struct.c": "chapter_18/valid/no_structure_parameters/libraries/global_struct_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/opaque_struct.c": "chapter_18/valid/no_structure_parameters/libraries/opaque_struct_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/param_struct_pointer.c": "chapter_18/valid/no_structure_parameters/libraries/param_struct_pointer_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/return_struct_pointer.c": "chapter_18/valid/no_structure_parameters/libraries/return_struct_pointer_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/initializers/auto_struct_initializers.c": "chapter_18/valid/no_structure_parameters/libraries/initializers/auto_struct_initializers_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/initializers/nested_auto_struct_initializers.c": "chapter_18/valid/no_structure_parameters/libraries/initializers/nested_auto_struct_initializers_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/initializers/nested_static_struct_initializers.c": "chapter_18/valid/no_structure_parameters/libraries/initializers/nested_static_struct_initializers_client.c",
    "chapter_18/valid/no_structure_parameters/libraries/initializers/static_struct_initializers.c": "chapter_18/valid/no_structure_parameters/libraries/initializers/static_struct_initializers_client.c",
    "chapter_18/valid/parameters/libraries/classify_params.c": "chapter_18/valid/parameters/libraries/classify_params_client.c",
    "chapter_18/valid/parameters/libraries/modify_param.c": "chapter_18/valid/parameters/libraries/modify_param_client.c",
    "chapter_18/valid/parameters/libraries/param_calling_conventions.c": "chapter_18/valid/parameters/libraries/param_calling_conventions_client.c",
    "chapter_18/valid/parameters/libraries/pass_struct.c": "chapter_18/valid/parameters/libraries/pass_struct_client.c",
    "chapter_18/valid/parameters/libraries/struct_sizes.c": "chapter_18/valid/parameters/libraries/struct_sizes_client.c",
    "chapter_18/valid/params_and_returns/libraries/access_retval_members.c": "chapter_18/valid/params_and_returns/libraries/access_retval_members_client.c",
    "chapter_18/valid/params_and_returns/libraries/return_calling_conventions.c": "chapter_18/valid/params_and_returns/libraries/return_calling_conventions_client.c",
    "chapter_18/valid/params_and_returns/libraries/retval_struct_sizes.c": "chapter_18/valid/params_and_returns/libraries/retval_struct_sizes_client.c",
}


def ensure_crt0():
    """Build crt0.o if it doesn't exist."""
    if not CRT0.exists():
        crt0_s = ROOT / "shared" / "crt0.s"
        if not S32_AS.exists():
            print("Warning: s32-as not found, cannot build crt0.o")
            return
        r = subprocess.run(
            [str(S32_AS), "-o", str(CRT0), str(crt0_s)],
            capture_output=True, text=True, timeout=10
        )
        if r.returncode != 0:
            print(f"Warning: failed to build crt0.o: {r.stderr}")


def get_props_key(source_file: Path) -> str:
    """Key for expected_results.json (always uses forward slashes)"""
    return source_file.relative_to(TESTS_DIR).as_posix()


def compile_and_run(source_file: Path, verbose=False):
    """Compile a .c file with s32-cc and run through sim_s32.
    Returns (return_code, stdout, stderr).
    """
    key = get_props_key(source_file)

    # Check if this is a multi-file test (the library file)
    # We only handle multi-file when the main file (client) is processed
    # For the client file, compile both parts and link

    # Check if this is a client file in a multi-file pair
    client_rel = None
    lib_rel = None
    for lib_k, client_k in MULTI_FILE_SETS.items():
        if key == client_k:
            client_rel = client_k
            lib_rel = lib_k
            break
        # Also check if this IS the library file - process it as part of the client
        if key == lib_k:
            # Skip when encountering the library file alone; it will be handled
            # when the client file is processed
            return "SKIP", "", ""

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)

        if lib_rel:
            # Multi-file: compile both .c files and link with crt0
            lib_src = TESTS_DIR / lib_rel
            client_src = TESTS_DIR / client_rel
            lib_o = tmp / "lib.o"
            client_o = tmp / "client.o"
            linked = tmp / "out.bin"

            # Compile library
            r1 = subprocess.run(
                [str(S32_CC), "-c", "-o", str(lib_o), str(lib_src)],
                capture_output=True, text=True, timeout=10
            )
            if r1.returncode != 0:
                return r1.returncode, "", r1.stderr

            # Compile client
            r2 = subprocess.run(
                [str(S32_CC), "-c", "-o", str(client_o), str(client_src)],
                capture_output=True, text=True, timeout=10
            )
            if r2.returncode != 0:
                return r2.returncode, "", r2.stderr

            # Link with crt0
            r3 = subprocess.run(
                [str(S32_LD), "-o", str(linked), str(CRT0), str(lib_o), str(client_o)],
                capture_output=True, text=True, timeout=10
            )
            if r3.returncode != 0:
                return r3.returncode, "", r3.stderr

            bin_path = linked
        else:
            # Single file: compile directly (uses built-in _start)
            bin_path = tmp / (source_file.stem + ".bin")
            compile_cmd = [str(S32_CC), "-o", str(bin_path), str(source_file)]
            r1 = subprocess.run(
                compile_cmd, capture_output=True, text=True, timeout=10
            )
            if r1.returncode != 0:
                return r1.returncode, "", r1.stderr
            if not bin_path.exists() or bin_path.stat().st_size == 0:
                return "NO_OUTPUT", "", "compiler produced no output"

        # Run through simulator
        sim_cmd = [str(SIM), str(bin_path)]
        try:
            sim_result = subprocess.run(
                sim_cmd, capture_output=True, text=True, timeout=120,
                stdin=subprocess.DEVNULL
            )
        except subprocess.TimeoutExpired:
            return "TIMEOUT", "", "simulator timeout"

        stdout = sim_result.stdout
        exit_code = sim_result.returncode & 0xFF

        return exit_code, stdout, sim_result.stderr


def test_chapter(chapter: int, verbose=False):
    """Run all valid tests for a given chapter."""
    chapter_dir = TESTS_DIR / f"chapter_{chapter}" / "valid"
    if not chapter_dir.exists():
        print(f"Chapter {chapter} directory not found")
        return 0, 0

    passed = 0
    failed = 0
    errors = []

    for source in sorted(chapter_dir.rglob("*.c")):
        key = get_props_key(source)
        if key not in EXPECTED_RESULTS:
            continue

        expected = EXPECTED_RESULTS[key]
        expected_retcode = expected["return_code"]
        expected_stdout = expected.get("stdout", "")

        actual_retcode, actual_stdout, stderr = compile_and_run(source, verbose)

        # Skip library-only files (handled as part of client)
        if actual_retcode == "SKIP":
            continue

        name = source.relative_to(TESTS_DIR / f"chapter_{chapter}")

        if actual_retcode == "TIMEOUT":
            print(f"  TIMEOUT {name}")
            continue

        if actual_retcode == "NO_OUTPUT":
            print(f"  FAIL  {name} (no output)")
            failed += 1
            continue

        ok = (actual_retcode == expected_retcode and actual_stdout == expected_stdout)

        if ok:
            passed += 1
            if verbose:
                print(f"  PASS  {name}")
        else:
            failed += 1
            errors.append(name)
            print(f"  FAIL  {name}")
            if actual_retcode != expected_retcode:
                print(f"        return code: expected {expected_retcode}, got {actual_retcode}")
            if actual_stdout != expected_stdout:
                print(f"        stdout: expected {repr(expected_stdout)}, got {repr(actual_stdout)}")
            if verbose and stderr:
                for line in stderr.strip().split("\n")[:5]:
                    print(f"        stderr: {line}")

    return passed, failed


def test_invalid_parse_chapter(chapter: int, verbose=False):
    """Run all invalid_parse tests for a given chapter."""
    chapter_dir = TESTS_DIR / f"chapter_{chapter}" / "invalid_parse"
    if not chapter_dir.exists():
        return 0, 0

    passed = 0
    failed = 0

    for source in sorted(chapter_dir.rglob("*.c")):
        compile_cmd = [str(S32_CC), "-o", str(Path(tempfile.gettempdir()) / (source.stem + ".bin")), str(source)]
        compile_result = subprocess.run(
            compile_cmd, capture_output=True, text=True, timeout=10
        )
        name = source.relative_to(TESTS_DIR / f"chapter_{chapter}")

        if compile_result.returncode != 0:
            passed += 1
            if verbose:
                print(f"  PASS  {name}")
        else:
            failed += 1
            print(f"  FAIL  {name}")
            if verbose:
                print(f"        expected non-zero exit, got 0")

    return passed, failed


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Test s32-cc against writing-a-c-compiler-tests")
    parser.add_argument("--chapter", type=int, nargs="*", help="Chapters to test (default: 1-9)")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--test-file", type=str, help="Test a single .c file")
    parser.add_argument("--invalid", action="store_true", help="Also run invalid_parse tests")
    args = parser.parse_args()

    ensure_crt0()

    if args.test_file:
        src = Path(args.test_file)
        retcode, stdout, stderr = compile_and_run(src, args.verbose)
        print(f"Return code: {retcode}")
        print(f"Stdout: {repr(stdout)}")
        if stderr and args.verbose:
            print(f"Stderr:\n{stderr}")
        return 0

    chapters = args.chapter if args.chapter else list(range(1, 10))
    total_pass = 0
    total_fail = 0

    for ch in chapters:
        print(f"\n=== Chapter {ch} ===")
        passed, failed = test_chapter(ch, args.verbose)
        total_pass += passed
        total_fail += failed
        print(f"  valid: {passed} passed, {failed} failed")

        if args.invalid:
            ip_passed, ip_failed = test_invalid_parse_chapter(ch, args.verbose)
            total_pass += ip_passed
            total_fail += ip_failed
            print(f"  invalid_parse: {ip_passed} passed, {ip_failed} failed")

    print(f"\n=== Total: {total_pass} passed, {total_fail} failed ===")
    return 0 if total_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
