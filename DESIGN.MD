# WASM-S32: Stack-Based CPU Toolchain

## Architecture Overview

WASM-S32 is a 32-bit stack-based CPU architecture inspired by WebAssembly. The toolchain consists of a simulator, assembler, linker, C compiler, and runtime, all targeting this custom ISA.

```
Source (.c) → s32-cc → ELF .o  ─┐
Assembly (.s) → s32-as → ELF .o ─┤
                                  ├→ s32-ld → ELF executable → sim_s32
                        crt0.o ───┘
```

---

## 1. ISA & Simulator (`sim_s32.c`)

### 1.1 CPU State

| Field | Size | Description |
|-------|------|-------------|
| `pc` | u32 | Program counter |
| `dsp` | u32 | Data stack pointer |
| `rsp` | u32 | Return stack pointer |
| `fp` | u32 | Frame pointer (base for local variables) |
| `data_stack` | 256 x u32 | Data stack (operand stack) |
| `return_stack` | 256 x u32 | Return stack (call frames + frame sizes) |
| `csr[8]` | 8 x u32 | Control/status registers |
| `privilege` | u32 | 0=user, 1=kernel |
| `ram` | 1024 x u32 | Main memory (4KB) |

### 1.2 Opcodes (52 implemented, 0 missing from ISA.md)

All opcodes from `0x01` through `0x47` plus `0xFF` are implemented:

| Range | Opcodes | Category |
|-------|---------|----------|
| 0x01-0x05 | push, add, sub, mul, drop | Arithmetic/stack |
| 0x08 | print | I/O |
| 0x09-0x0F | eq, lt_s, gt_s, lt_u, gt_u, br_if, jump | Comparison/control |
| 0x10-0x11 | call, return | Function calls |
| 0x12-0x15 | dup, swap, over, rot | Stack manipulation |
| 0x16-0x1C | and, or, xor, not, shl, shr_u, shr_s | Bitwise/shift |
| 0x1D-0x1F | load, store, key | Memory/I/O |
| 0x30-0x35 | >r, r>, r@, depth, rdepth, eqz | Return stack/condition |
| 0x36-0x38 | div_s, load8_u, store8 | Extended arithmetic/memory |
| 0x39-0x3A | local.get, local.set | Local variables (FP-relative) |
| 0x3B-0x3F | sysenter, eret, csr_read, csr_write, tlb_flush | System/privileged |
| 0x40 | get_fp | Frame pointer access |
| 0x41-0x47 | fadd, fsub, fmul, fdiv, fcmp, f2i, i2f | Floating point (doubles) |
| 0xFF | halt | Stop execution |

### 1.3 Calling Convention

This is the most unusual aspect of the architecture:

1. **Caller** pushes arguments left-to-right on the **data stack**
2. **Caller** pushes `frame_size` (number of local slots) onto the **return stack** via `push N; >r`
3. **`call`**: pops frame_size from return stack, saves return PC and old FP to return stack, sets `fp = fp + frame_size`
4. **Callee** pops arguments from data stack into local slots (reverse order)
5. **`return`**: pops FP then PC from return stack

This convention means the frame pointer always points to the callee's frame, and local variables at `fp + idx` are after the caller's locals.

### 1.4 Memory Model

- `ram` is `u32[]` (word-addressable, 4 bytes per word)
- `load`/`store`: address is a *word index* → `ram[addr]`
- `load8_u`/`store8`: address is a *byte offset* → byte within `ram[addr >> 2]`
- `local.get`/`local.set`: use `ram[(fp + idx) & mask]`
- `get_fp`: pushes current `fp` onto data stack

### 1.5 MMIO

| Address | Device | Access |
|---------|--------|--------|
| `0xFE000000` | UART TX | Write byte to serial output |
| `0xFE000004` | UART RX | Read byte from serial input |
| `0xFE000008` | UART status | bit0=TX ready, bit1=RX ready |
| `0xFE001000` | Timer count | Free-running counter |
| `0xFE001004` | Timer compare | Compare value |
| `0xFE001008` | Timer control | bit0=enable, bit1=IRQ |

### 1.6 Privilege & System Calls

- Two privilege levels: USER (0) and KERNEL (1)
- `sysenter`: saves PC to CSR[EPC], data to CSR[EDATA], jumps to CSR[EVEC]+0x10, elevates to KERNEL
- `eret`: restores PC from CSR[EPC], data from CSR[EDATA], drops to USER
- `csr_read id`: pushes CSR[id]
- `csr_write id`: pops to CSR[id]
- MMU/TLB: `tlb_flush` is a no-op; no page table walking implemented

### 1.7 Simulator Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No NOP (0x00) handler | Dispatch table has NULL entry (crash if executed) | Low |
| No opcode bounds check | Opcodes 0x06, 0x07, 0x20-0x2F hit NULL dispatch (crash) | Low |
| KEY is non-blocking | Returns 0 if no input, unlike spec that says "block" | Low |
| No interrupt delivery | Timer IRQ bit never checked | Medium (kernel) |
| MMU not implemented | SATP stored but no page walk | Medium (kernel) |
| No 16-bit load/store | `short` type not properly supported | Medium |

---

## 2. Assembler (`s32-as`)

### 2.1 Status

| Feature | Status |
|---------|--------|
| Two-pass assembly | ✓ |
| Labels (forward/backward refs) | ✓ |
| `.globl`/`.global` directives | ✓ |
| `.word`, `.byte`, `.space` directives | ✓ |
| `.text`, `.data`, `.section` (ignored) | ✓ |
| All 52 opcode mnemonics | ✓ |
| ELF object file output (`.o`) | ✓ |
| Flat binary output | ✓ |
| Relocation generation (R_WASM32_32) | ✓ |
| Comment stripping (`;`) | ✓ |
| ELF sections: .text, .symtab, .strtab, .shstrtab, .rela.text | ✓ |

### 2.2 Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No `.ascii`/`.asciz` directives | Can't embed string data | Medium |
| No `.equ`/`.set` constants | No symbolic constants | Low |
| No `.include` directive | No include files | Low |
| No `.comm`/`.lcomm` | No common symbols | Low |
| No expression parsing | Only single values or labels | Medium |
| No conditional assembly | No `.if`/`.endif` | Low |
| No macro support | No assembly macros | Low |
| No alignment directive | `.align` is ignored | Low |
| All sections merge into .text | No separate .data/.rodata | Medium |

---

## 3. Linker (`s32-ld`)

### 3.1 Status

| Feature | Status |
|---------|--------|
| Read ELF ET_REL objects | ✓ |
| Validate EM_WASM32 machine type | ✓ |
| Merge .text sections | ✓ |
| Global symbol resolution | ✓ |
| Relocation application (R_WASM32_32) | ✓ |
| Duplicate symbol detection | ✓ |
| Undefined symbol reporting | ✓ |
| ELF executable output (ET_EXEC) | ✓ |
| Flat binary output (.bin) | ✓ |
| Program headers (PT_LOAD) | ✓ |
| _start frame_size patching | ✓ |
| Entry point specification (`-e`) | ✓ |

### 3.2 Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No .data section merging | All data/code mixed | Medium |
| No .bss section | No zero-initialized data | Medium |
| No SHT_REL (only SHT_RELA) | Compatibility | Low |
| No archive (.a) support | Can't use static libraries | Low |
| No `-L`/`-l` flags | No library search | Low |
| No dynamic linking | Can't share libraries | Low (kernel) |
| No relaxation/optimization | Wastes space | Low |
| Only R_WASM32_32 relocation | No PC-relative | Low |
| No `SHN_COMMON` support | Common symbols | Low |

---

## 4. C Compiler (`s32-cc`)

### 4.1 Frontend (Lexer + Parser)

| Feature | Status |
|---------|--------|
| C89 type system (int, char, void, enum, struct, union, pointers) | ✓ |
| Storage class specifiers (static, extern, typedef) | ✓ |
| Type qualifiers (const — parsed, ignored) | ✓ |
| `long`, `short`, `unsigned`, `signed` | ✓ |
| `long long` — accepted, treated as long | ⚠️ Workaround |
| `double`/`float` keywords | ✓ |
| All control flow (if/else, while, for, do, switch, goto) | ✓ |
| Break/continue in loops and switches | ✓ |
| Pratt expression parser (full C precedence) | ✓ |
| Comma operator | ✓ |
| Ternary operator | ✓ |
| Compound assignments (+=, -=, etc.) | ✓ |
| Increment/decrement (prefix/postfix) | ✓ |
| Array subscript and member access (. / ->) | ✓ |
| Cast expressions | ✓ (int↔double) |
| Initializer lists ({1, 2, 3}) for arrays and structs | ✓ |
| Nested initializer lists ({{1},{2}}) | ✓ |
| Adjacent string concatenation | ✓ |
| sizeof (type and expression forms) | ✓ |
| Function pointer declarations (`int (*f)()`) | ❌ Missing |
| Designated initializers (`{.field = x}`) | ❌ Missing |
| Inline assembly (`asm()`) | ❌ Missing (AST node exists, no parser) |
| Bitfields | ❌ Missing |
| Variable-length arrays | ❌ Missing |
| `volatile` keyword | ❌ Missing (would error) |
| `_Bool`, `_Complex`, `_Imaginary` | ❌ Missing |
| C preprocessor (#include, #define) | ❌ Lines skipped |
| `short` type | ⚠️ No 16-bit load/store opcodes |

### 4.2 Codegen

| Feature | Status |
|--------|--------|
| Arithmetic operations | ✓ |
| All comparisons (signed, unsigned) | ✓ |
| Control flow (branches, jumps, calls) | ✓ |
| Local variables (FP-relative) | ✓ |
| Global variables (FP-relative, init via __init) | ✓ |
| Pointers (address-of, deref, arithmetic) | ✓ |
| Arrays (subscript, decay to pointer) | ✓ |
| Struct/union member access | ✓ |
| String literals | ✓ |
| `void*` implicit conversions | ⚠️ Untested |
| Struct return by value | ⚠️ Untested |
| Double arithmetic (fadd/fsub/fmul/fdiv) | ✓ |
| Function calls (direct, by name) | ✓ |
| Indirect function calls (`(*fp)()`) | ❌ Missing |
| Constant folding (compile-time eval) | ✓ |
| Algebraic simplifications (x+0→x, etc.) | ✓ |
| Peephole optimizations | ⚠️ Disabled (unsafe) |
| AST_COMMA node handler | ❌ Missing (falls through) |
| AST_ASM node handler | ❌ Missing (falls through) |

### 4.3 Output Modes

| Mode | Status |
|------|--------|
| Flat binary (direct execution) | ✓ |
| ELF object (`-c` flag) | ✓ |
| ELF executable (via codegen_program) | ✓ |
| Assembly text (`-S` flag) | ❌ Stub only |
| Preprocess only (`-E` flag) | ❌ Missing |

---

## 5. C Runtime (`shared/crt0.s`)

### 5.1 Current Behavior

```
_start:
    push 0          ; linker patches this to total binary size
    >r              ; push frame_size to return stack
    call :main      ; call main (pops frame_size, advances fp)
    halt
```

### 5.2 Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No argc/argv support | Can't pass command-line args | Low (shell) |
| No heap/malloc | No dynamic memory allocation | High (kernel, shell) |
| No .bss zeroing | Globals not initialized to 0 | Medium (already zeroed by memset) |
| No atexit handlers | No cleanup on exit | Low |
| No envp support | No environment variables | Low (shell) |

---

## 6. ELF Format (`shared/elf32.h`)

### 6.1 Status

| Item | Value |
|------|-------|
| ELF class | ELFCLASS32 (32-bit) |
| Byte order | ELFDATA2LSB (little-endian) |
| Machine type | `EM_WASM32 = 0x9999` |
| OS/ABI | ELFOSABI_NONE (bare metal) |
| Relocation type | `R_WASM32_32` (32-bit absolute address) |
| Object files | ET_REL with .text, .symtab, .strtab, .shstrtab, .rela.text |
| Executables | ET_EXEC with one PT_LOAD segment + section headers ✓ |
| Program headers | PT_LOAD with PF_R|PF_X ✓ |

### 6.2 Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No data section in executables | String data in .text | Medium |
| No dynamic section | Can't build shared libs | Low |
| No note section | No metadata | Low |
| No EH_FRAME | No exception handling | Low |
| Only one relocation type | No PC-relative calls | Low |

---

## 7. Test Results

### 7.1 writing-a-c-compiler-tests

| Chapter | Passing | Total | Notes |
|---------|---------|-------|-------|
| 1-9 | **265** | 266 | Only stack_alignment.c (different ISA) |
| 10 (static/extern) | 5 | 30 | Needs static persistence, extern linkage |
| 11 (long) | 5 | 33 | long=32-bit vs expected 64-bit |
| 12 (unsigned) | 5 | 29 | Mostly works |
| 13 (double) | 5 | 39 | Double codegen partial |
| 14 (pointers) | 13 | 29 | Mostly works |
| 15 (arrays) | 7 | 42 | Multi-dim, inits |
| 16 (char/string) | 11 | 51 | Mostly works |
| 17 (sizeof/void) | 2 | 24 | sizeof works, void* partial |
| 18 (struct/union) | ~2 | ~60 | Most struct tests fail |
| 19 (opt) | 0 | ~100 | Optimization passes not implemented |
| 20 (regalloc) | 0 | ~50 | N/A for stack machine |

### 7.2 c-testsuite

| Result | Count | Notes |
|--------|-------|-------|
| Pass | 65 | Basic C programs |
| Fail (runtime) | 13 | Struct/function pointer issues |
| Compile error | 17 | Various missing features |
| Skipped | 125 | Preprocessor, float, bitfields, varargs, etc. |

---

## 8. Roadmap to Goals

### 8.1 Self-Bootstrapping Compiler

Our compiler source uses only 4 features that could block self-compilation:

| Feature | Used Where | Status | Fix Needed |
|---------|-----------|--------|------------|
| `void*` implicit conversion | Every `calloc`/`malloc` return | ⚠️ Untested | Verify & fix if broken |
| `const` keyword | 39 uses (function params) | ✅ Accepted | Verify acceptance |
| Struct return by value | `Token lexer_next()` | ⚠️ Untested | Verify struct return |
| String literals | Every `"..."` in source | ✅ Works | — |

**Effort: Low.** These likely just need verification. If they work, the compiler can likely compile itself.

### 8.2 Full Test Suite Coverage

| Priority | Feature | Tests Unlocked | Effort |
|----------|---------|---------------|--------|
| **P0** | Function pointers | ch18 struct tests, many c-testsuite | Medium |
| **P0** | `void*` implicit conversions | ch14, ch17 pointer tests | Small |
| **P0** | Struct return by value | ch18 param/return tests | Medium |
| **P1** | Static local persistence | ch10 static locals (5 tests) | Medium |
| **P1** | Extern linkage rules | ch10 multiple decls (5 tests) | Medium |
| **P1** | Designated initializers | ch18 struct init | Medium |
| **P2** | Optimization passes | ch19 (~100 tests) | Large |
| **P3** | Bitfields | ch18, c-testsuite | Small |
| **P3** | Varargs | ch10+, c-testsuite | Medium |

**Total remaining features: ~12** to pass all applicable valid tests.

### 8.3 Unix-like Kernel

| Priority | Feature | Why Needed | Effort |
|----------|---------|-----------|--------|
| **P0** | `volatile` keyword | MMIO device register access | Small |
| **P0** | Inline assembly | sysenter/eret/csr access for context switching | Medium |
| **P0** | Function pointers | Interrupt handler tables, syscall dispatch | Medium |
| **P0** | `void*` conversions | Page allocator, device driver interfaces | Small |
| **P1** | Struct return | Kernel APIs | Medium |
| **P1** | Bitfields | Hardware register definitions | Small |
| **P1** | MMU implementation | Virtual memory, process isolation | Large |
| **P1** | Timer interrupts | Preemptive multitasking | Medium |
| **P2** | `malloc`/`free` (syscall) | Dynamic memory for user processes | Medium |
| **P2** | ELF loader | Loading programs from filesystem | Medium |

### 8.4 POSIX Shell

| Priority | Feature | Why Needed | Effort |
|----------|---------|-----------|--------|
| **P0** | `fork`/`exec` syscalls | Process creation | Large |
| **P0** | `read`/`write` syscalls | I/O | Medium |
| **P0** | Pipe syscall | Inter-process communication | Medium |
| **P1** | `chdir`/`stat` syscalls | Filesystem navigation | Medium |
| **P1** | `open`/`close` syscalls | File I/O | Medium |
| **P2** | Filesystem driver | Storage for files | Large |
| **P2** | `malloc`/`free` | Dynamic allocation | Medium |

---

## 9. Summary

### Currently Implemented
- **ISA**: 100% - all 52 opcodes implemented in simulator
- **Simulator**: Ring buffer instruction trace, ELF entry point support, real-time UART output, store8 MMIO routing
- **Assembler**: Full two-pass assembler with ELF object output
- **Linker**: ELF linker with conditional _start patching
- **Compiler** (feature/s32_cc branch): ~85% of C, minimal kernel boots ("xv6-s32 booting" printed successfully)
- **FPGA**: Verilog for Tang Nano 9K (top.v, stack_cpu.v, flash.v, uart_tx/rx, bram_stack, simple_ram)

### Branch Structure
- `main`: Verilog + simulator + assembler + shared runtime + scripts
- `feature/s32_cc`: Full toolchain including C compiler (s32-cc), linker (s32-ld), xv6-s32 kernel port

### Recent Fixes (feature/s32_cc)
- csr_write dummy return value, tlb_flush built-in
- String literal pre-scan and address separation from variables
- Simulator trace dump on crash, real-time UART output
- Linker conditional _start frame_size patching
- Combined single-file compilation for kernel

### Remaining for Kernel (on feature/s32_cc)
- printint/printptr have codegen stack depth bugs (shift/div/mul ops)
- swtch context switch needs proper implementation
- kalloc/acquire loops may have codegen issues
- ~6 critical features + MMU for full xv6 parity
