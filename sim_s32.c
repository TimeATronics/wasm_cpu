/*
 * sim_s32.c - WASM-S32 Reference Simulator (Phase 1)
 *
 * Golden reference for the ISA. Executes WASM-S32 bytecode on the host.
 * Features: full instruction set, CSR registers, privilege modes,
 * trap dispatch, MMU (stubbed), MMIO devices, trace logging.
 *
 * Build: gcc -O2 -o sim_s32 sim_s32.c
 * Usage: ./sim_s32 [-v] [-m max_steps] [-d] <binary.bin>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "shared/elf32.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* ── Configuration ─────────────────────────────────────────────── */

#define RAM_WORDS       1280          /* 5 KB (1024 for RAM + 256 for OLED framebuffer) */
#define RAM_BYTES        (RAM_WORDS * 4)
#define MAX_STACK        256          /* data stack depth */
#define MAX_RSP          256          /* return stack depth */
#define MMIO_BASE        0xFE000000u
#define MMIO_UART_TX     (MMIO_BASE + 0x00)
#define MMIO_UART_RX     (MMIO_BASE + 0x04)
#define MMIO_UART_STATUS (MMIO_BASE + 0x08)
#define MMIO_TIMER_COUNT (MMIO_BASE + 0x1000)
#define MMIO_TIMER_CMP   (MMIO_BASE + 0x1004)
#define MMIO_TIMER_CTRL  (MMIO_BASE + 0x1008)

/* CSR IDs */
#define CSR_STATUS  0
#define CSR_SATP    1
#define CSR_EVEC    2
#define CSR_EPC     3
#define CSR_EDATA   4
#define CSR_COUNT   5

/* Privilege bits in STATUS */
#define STATUS_KM   (1u << 0)  /* kernel mode active */
#define STATUS_UM   (1u << 1)  /* user mode active */
#define STATUS_KIE  (1u << 6)  /* kernel interrupt enable */
#define STATUS_UIE  (1u << 7)  /* user interrupt enable */

/* Privilege constants */
#define PRIV_USER   0
#define PRIV_KERNEL 1

/* ── Opcode names (for trace) ──────────────────────────────────── */

static const char *opcode_names[256] = {
    [0x01] = "push",     [0x02] = "add",      [0x03] = "sub",
    [0x04] = "mul",      [0x05] = "drop",     [0x08] = "print",
    [0x09] = "eq",       [0x0A] = "lt_s",     [0x0B] = "gt_s",
    [0x0C] = "lt_u",     [0x0D] = "gt_u",     [0x0E] = "br_if",
    [0x0F] = "jump",     [0x10] = "call",     [0x11] = "return",
    [0x12] = "dup",      [0x13] = "swap",     [0x14] = "over",
    [0x15] = "rot",      [0x16] = "and",      [0x17] = "or",
    [0x18] = "xor",      [0x19] = "not",      [0x1A] = "shl",
    [0x1B] = "shr_u",    [0x1C] = "shr_s",
    [0x1D] = "load",     [0x1E] = "store",    [0x1F] = "key",
    [0x30] = ">r",       [0x31] = "r>",       [0x32] = "r@",
    [0x33] = "depth",    [0x34] = "rdepth",   [0x35] = "eqz",
    [0x36] = "div_s",    [0x37] = "load8_u",  [0x38] = "store8",
    [0x39] = "local.get",[0x3A] = "local.set",
    [0x3B] = "sysenter", [0x3C] = "eret",
    [0x3D] = "csr_read", [0x3E] = "csr_write",[0x3F] = "tlb_flush",
    [0x40] = "get_fp",    [0x49] = "set_fp",
    [0x41] = "fadd",     [0x42] = "fsub",     [0x43] = "fmul",
    [0x44] = "fdiv",     [0x45] = "fcmp",     [0x46] = "f2i",
    [0x47] = "i2f",      [0x48] = "call_ind",
    [0xFF] = "halt",
};

/* ── Types ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t pc;
    uint32_t dsp;            /* data stack pointer (index) */
    uint32_t rsp;            /* return stack pointer (index) */
    uint32_t fp;             /* frame pointer */
    uint32_t data_stack[MAX_STACK];
    uint32_t return_stack[MAX_RSP];

    /* CSR registers */
    uint32_t csr[8];

    /* Privilege state */
    int privilege;           /* PRIV_USER or PRIV_KERNEL */

    /* Memory */
    uint32_t ram[RAM_WORDS];

    /* MMIO state */
    uint32_t uart_status;    /* bit0=TX ready, bit1=RX ready */
    uint32_t timer_count;
    uint32_t timer_cmp;
    uint32_t timer_ctrl;     /* bit0=enable, bit1=enable_irq */
    char     *input_buf;
    size_t    input_pos;
    size_t    input_len;

    /* Control */
    bool     halted;
    bool     trace;
    uint64_t steps;
    uint64_t max_steps;

    /* Output capture */
    char     output[65536];
    size_t   output_len;
} cpu_t;

/* ── Ring-buffer instruction trace ──────────────────────────────── */

#define TRACE_SIZE  256
typedef struct {
    uint32_t pc;
    uint8_t  opcode;
    int      dsp;
    int      rsp;
    uint32_t top_val;
} TraceEntry;

static TraceEntry trace_buf[TRACE_SIZE];
static int trace_idx = 0;

static void trace_record(cpu_t *cpu, uint8_t opcode) {
    trace_buf[trace_idx] = (TraceEntry){
        .pc = cpu->pc, .opcode = opcode,
        .dsp = cpu->dsp, .rsp = cpu->rsp,
        .top_val = (cpu->dsp > 0) ? cpu->data_stack[cpu->dsp - 1] : 0
    };
    trace_idx = (trace_idx + 1) % TRACE_SIZE;
}

static void dump_trace(void) {
    fprintf(stderr, "\n=== LAST %d INSTRUCTIONS ===\n", TRACE_SIZE);
    fprintf(stderr, "  PC      | Opcode       | DSP | RSP | Top\n");
    fprintf(stderr, "----------+--------------+-----+-----+--------\n");
    for (int i = 0; i < TRACE_SIZE; i++) {
        TraceEntry *e = &trace_buf[(trace_idx + i) % TRACE_SIZE];
        if (e->opcode == 0) continue;
        const char *name = opcode_names[e->opcode];
        fprintf(stderr, "0x%08X | %-12s | %3d | %3d | 0x%x\n",
                e->pc, name ? name : "???", e->dsp, e->rsp, e->top_val);
    }
    fprintf(stderr, "============================\n\n");
}

static void fatal(cpu_t *cpu, const char *msg) {
    fprintf(stderr, "%s at PC=0x%08X (step %llu)\n", msg, cpu->pc, (unsigned long long)cpu->steps);
    dump_trace();
    exit(1);
}

/* ── Helpers ───────────────────────────────────────────────────── */

static inline uint32_t to_unsigned(int32_t v) { return (uint32_t)v; }
static inline int32_t  to_signed(uint32_t v) {
    return (v < 0x80000000u) ? (int32_t)v : (int32_t)(v - 0x100000000u);
}

static inline void push(cpu_t *cpu, uint32_t val) {
    if (cpu->dsp >= MAX_STACK)
        fatal(cpu, "Data stack overflow");
    cpu->data_stack[cpu->dsp++] = val;
}

static inline uint32_t pop(cpu_t *cpu) {
    if (cpu->dsp == 0)
        fatal(cpu, "Data stack underflow");
    return cpu->data_stack[--cpu->dsp];
}

static inline void rpush(cpu_t *cpu, uint32_t val) {
    if (cpu->rsp >= MAX_RSP)
        fatal(cpu, "Return stack overflow");
    cpu->return_stack[cpu->rsp++] = val;
}

static inline uint32_t rpop(cpu_t *cpu) {
    if (cpu->rsp == 0)
        fatal(cpu, "Return stack underflow");
    return cpu->return_stack[--cpu->rsp];
}

static inline uint32_t peek(cpu_t *cpu, int offset) {
    int idx = (int)cpu->dsp - 1 - offset;
    if (idx < 0) {
        fprintf(stderr, "Stack peek underflow at PC=0x%08X\n", cpu->pc);
        exit(1);
    }
    return cpu->data_stack[idx];
}

/* Read a 32-bit little-endian immediate from program[pc], advance pc */
static uint32_t read_imm32(cpu_t *cpu, const uint8_t *program, size_t prog_len) {
    if (cpu->pc + 4 > prog_len) {
        fprintf(stderr, "PC out of bounds reading imm32 at 0x%08X\n", cpu->pc);
        exit(1);
    }
    uint32_t val = program[cpu->pc]
                 | (program[cpu->pc + 1] << 8)
                 | (program[cpu->pc + 2] << 16)
                 | (program[cpu->pc + 3] << 24);
    cpu->pc += 4;
    return val;
}

static uint8_t read_imm8(cpu_t *cpu, const uint8_t *program, size_t prog_len) {
    if (cpu->pc >= prog_len) {
        fprintf(stderr, "PC out of bounds reading imm8 at 0x%08X\n", cpu->pc);
        exit(1);
    }
    return program[cpu->pc++];
}

/* ── Memory access ─────────────────────────────────────────────── */

static uint32_t ram_read(cpu_t *cpu, uint32_t addr) {
    uint32_t idx = (addr >> 2) & (RAM_WORDS - 1);
    return cpu->ram[idx];
}

static void ram_write(cpu_t *cpu, uint32_t addr, uint32_t val) {
    uint32_t idx = (addr >> 2) & (RAM_WORDS - 1);
    cpu->ram[idx] = val;
}

/* Full memory read (checks MMIO first, falls back to RAM) */
static uint32_t mem_read(cpu_t *cpu, uint32_t addr) {
    if (addr >= MMIO_BASE) {
        switch (addr) {
        case MMIO_UART_RX:
            /* Return next input character, or 0 if none */
            if (cpu->input_pos < cpu->input_len) {
                uint32_t ch = (uint32_t)(unsigned char)cpu->input_buf[cpu->input_pos++];
                return ch;
            }
            return 0;
        case MMIO_UART_STATUS:
            /* bit0=TX ready(1), bit1=RX ready */
            return 0x01 | ((cpu->input_pos < cpu->input_len) ? 0x02 : 0x00);
        case MMIO_TIMER_COUNT:
            return cpu->timer_count;
        case MMIO_TIMER_CMP:
            return cpu->timer_cmp;
        case MMIO_TIMER_CTRL:
            return cpu->timer_ctrl;
        default:
            return 0;
        }
    }
    return ram_read(cpu, addr);
}

static void mem_write(cpu_t *cpu, uint32_t addr, uint32_t val) {
    if (addr >= MMIO_BASE) {
        switch (addr) {
        case MMIO_UART_TX:
            /* Output character immediately (flush for interactive/debugging) */
            {
                char ch = (char)(val & 0xFF);
                fputc(ch, stdout);
                fflush(stdout);
            }
            return;
        case MMIO_TIMER_CMP:
            cpu->timer_cmp = val;
            return;
        case MMIO_TIMER_CTRL:
            cpu->timer_ctrl = val;
            return;
        default:
            return;
        }
    }
    ram_write(cpu, addr, val);
}

/* ── CSR access ────────────────────────────────────────────────── */

static uint32_t csr_read(cpu_t *cpu, uint32_t id) {
    if (id >= sizeof(cpu->csr) / sizeof(cpu->csr[0])) return 0;
    return cpu->csr[id];
}

static void csr_write(cpu_t *cpu, uint32_t id, uint32_t val) {
    if (id >= sizeof(cpu->csr) / sizeof(cpu->csr[0])) return;
    cpu->csr[id] = val;
    if (id == CSR_STATUS) {
        cpu->privilege = (val & STATUS_KM) ? PRIV_KERNEL : PRIV_USER;
    }
}

/* ── Trace ─────────────────────────────────────────────────────── */

static void trace_step(cpu_t *cpu, uint32_t start_pc, const char *name) {
    fprintf(stderr, "  PC=0x%08X %-12s  stack=[", start_pc, name);
    for (uint32_t i = 0; i < cpu->dsp; i++) {
        if (i > 0) fprintf(stderr, ", ");
        fprintf(stderr, "0x%08X", cpu->data_stack[i]);
    }
    fprintf(stderr, "]  rstack=[");
    for (uint32_t i = 0; i < cpu->rsp; i++) {
        if (i > 0) fprintf(stderr, ", ");
        fprintf(stderr, "0x%08X", cpu->return_stack[i]);
    }
    fprintf(stderr, "]\n");
}

/* ── Execution (computed-goto dispatch for speed) ──────────────── */

#define DISPATCH() goto *dispatch_table[opcode]

static int execute(cpu_t *cpu, const uint8_t *program, size_t prog_len) {
    static const void *dispatch_table[256] = {
        [0x01] = &&do_push,    [0x02] = &&do_add,     [0x03] = &&do_sub,
        [0x04] = &&do_mul,     [0x05] = &&do_drop,    [0x08] = &&do_print,
        [0x09] = &&do_eq,      [0x0A] = &&do_lt_s,    [0x0B] = &&do_gt_s,
        [0x0C] = &&do_lt_u,    [0x0D] = &&do_gt_u,    [0x0E] = &&do_br_if,
        [0x0F] = &&do_jump,    [0x10] = &&do_call,    [0x11] = &&do_return,
        [0x12] = &&do_dup,     [0x13] = &&do_swap,    [0x14] = &&do_over,
        [0x15] = &&do_rot,     [0x16] = &&do_and,     [0x17] = &&do_or,
        [0x18] = &&do_xor,     [0x19] = &&do_not,     [0x1A] = &&do_shl,
        [0x1B] = &&do_shr_u,   [0x1C] = &&do_shr_s,   [0x1D] = &&do_load,
        [0x1E] = &&do_store,   [0x1F] = &&do_key,     [0x30] = &&do_rpush,
        [0x31] = &&do_rpop,    [0x32] = &&do_rpeek,   [0x33] = &&do_depth,
        [0x34] = &&do_rdepth,  [0x35] = &&do_eqz,     [0x36] = &&do_div_s,
        [0x37] = &&do_load8_u, [0x38] = &&do_store8,  [0x39] = &&do_local_get,
        [0x3A] = &&do_local_set,
        [0x3B] = &&do_sysenter,[0x3C] = &&do_eret,    [0x3D] = &&do_csr_read,
        [0x3E] = &&do_csr_write,[0x3F] = &&do_tlb_flush,
        [0x40] = &&do_get_fp,
        [0x49] = &&do_set_fp,
        [0x41] = &&do_fadd,    [0x42] = &&do_fsub,    [0x43] = &&do_fmul,
        [0x44] = &&do_fdiv,    [0x45] = &&do_fcmp,    [0x46] = &&do_f2i,
        [0x47] = &&do_i2f,   [0x48] = &&do_call_ind,
        [0xFF] = &&do_halt,
    };

    uint8_t opcode;
    /* First fetch */
    if (cpu->pc >= prog_len) return 0;
    if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
    opcode = program[cpu->pc++];
    cpu->steps++;
    DISPATCH();

    do_push: {
        uint32_t val = read_imm32(cpu, program, prog_len);
        push(cpu, val);
        if (cpu->pc >= prog_len) return 0;
        if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
        opcode = program[cpu->pc++];
        cpu->steps++;
        DISPATCH();
    }
    do_add: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a + b);
        if (cpu->pc >= prog_len) return 0;
        if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
        opcode = program[cpu->pc++];
        cpu->steps++;
        DISPATCH();
    }
    do_sub: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a - b);
        if (cpu->pc >= prog_len) return 0;
        if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
        opcode = program[cpu->pc++];
        cpu->steps++;
        DISPATCH();
    }
    do_mul: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a * b);
        if (cpu->pc >= prog_len) return 0;
        if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
        opcode = program[cpu->pc++];
        cpu->steps++;
        DISPATCH();
    }
    do_div_s: {
        uint32_t b = pop(cpu), a = pop(cpu);
        if (b == 0) { push(cpu, 0); goto fetch_next; }
        int32_t sa = to_signed(a), sb = to_signed(b);
        push(cpu, to_unsigned(sa / sb));
        goto fetch_next;
    }
    do_drop:
        pop(cpu);
        goto fetch_next;
    do_print: {
        uint32_t val = pop(cpu);
            /* Fall through - print opcode output immediately */
            fputc((char)(val & 0xFF), stdout);
            fflush(stdout);
            goto fetch_next;
    }
    do_eq: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (a == b) ? 1 : 0);
        goto fetch_next;
    }
    do_lt_s: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (to_signed(a) < to_signed(b)) ? 1 : 0);
        goto fetch_next;
    }
    do_gt_s: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (to_signed(a) > to_signed(b)) ? 1 : 0);
        goto fetch_next;
    }
    do_lt_u: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (a < b) ? 1 : 0);
        goto fetch_next;
    }
    do_gt_u: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (a > b) ? 1 : 0);
        goto fetch_next;
    }
    do_eqz: {
        uint32_t a = pop(cpu);
        push(cpu, (a == 0) ? 1 : 0);
        goto fetch_next;
    }
    do_and: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a & b);
        goto fetch_next;
    }
    do_or: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a | b);
        goto fetch_next;
    }
    do_xor: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a ^ b);
        goto fetch_next;
    }
    do_not: {
        uint32_t a = pop(cpu);
        push(cpu, ~a);
        goto fetch_next;
    }
    do_shl: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a << (b & 31));
        goto fetch_next;
    }
    do_shr_u: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, a >> (b & 31));
        goto fetch_next;
    }
    do_shr_s: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, (uint32_t)(to_signed(a) >> (b & 31)));
        goto fetch_next;
    }
    do_dup: {
        uint32_t v = peek(cpu, 0);
        push(cpu, v);
        goto fetch_next;
    }
    do_swap: {
        uint32_t b = pop(cpu), a = pop(cpu);
        push(cpu, b);
        push(cpu, a);
        goto fetch_next;
    }
    do_over: {
        uint32_t v = peek(cpu, 1);
        push(cpu, v);
        goto fetch_next;
    }
    do_rot: {
        uint32_t c = pop(cpu), b = pop(cpu), a = pop(cpu);
        push(cpu, b);
        push(cpu, c);
        push(cpu, a);
        goto fetch_next;
    }
    do_depth:
        push(cpu, cpu->dsp);
        goto fetch_next;
    do_rdepth:
        push(cpu, cpu->rsp);
        goto fetch_next;
    do_load: {
        uint32_t addr = pop(cpu);
        push(cpu, mem_read(cpu, addr));
        goto fetch_next;
    }
    do_store: {
        uint32_t addr = pop(cpu), val = peek(cpu, 0);
        mem_write(cpu, addr, val);
        goto fetch_next;
    }
    do_load8_u: {
        uint32_t addr = pop(cpu);
        uint32_t word = mem_read(cpu, addr);
        uint32_t byte_idx = addr & 3;
        push(cpu, (word >> (byte_idx * 8)) & 0xFF);
        goto fetch_next;
    }
    do_store8: {
        uint32_t addr = pop(cpu), val = peek(cpu, 0);
        if (addr == 0x10000) {
            /* Frame sync: dump OLED framebuffer to stdout */
            unsigned long fb_start = 0x1000 / 4;
            fwrite(&cpu->ram[fb_start], 1, 1024, stdout);
            fflush(stdout);
        } else if (addr >= MMIO_BASE) {
            /* MMIO byte write: use mem_write for UART TX */
            mem_write(cpu, addr, val & 0xFF);
        } else {
            uint32_t word_idx = (addr >> 2) & (RAM_WORDS - 1);
            uint32_t byte_idx = addr & 3;
            uint32_t old_word = cpu->ram[word_idx];
            uint32_t mask = ~(0xFFu << (byte_idx * 8));
            uint32_t new_word = (old_word & mask) | ((val & 0xFF) << (byte_idx * 8));
            cpu->ram[word_idx] = new_word;
        }
        goto fetch_next;
    }
    do_local_get: {
        uint8_t idx = program[cpu->pc++];
        push(cpu, cpu->ram[(cpu->fp + idx) & (RAM_WORDS - 1)]);
        goto fetch_next;
    }
    do_local_set: {
        uint8_t idx = program[cpu->pc++];
        cpu->ram[(cpu->fp + idx) & (RAM_WORDS - 1)] = pop(cpu);
        goto fetch_next;
    }
    do_br_if: {
        uint32_t target = read_imm32(cpu, program, prog_len);
        uint32_t cond = pop(cpu);
        if (cond != 0) cpu->pc = target;
        goto fetch_next;
    }
    do_jump:
        cpu->pc = read_imm32(cpu, program, prog_len);
        goto fetch_next;
    do_call: {
        uint32_t target = read_imm32(cpu, program, prog_len);
        rpush(cpu, cpu->pc);
        cpu->pc = target;
        goto fetch_next;
    }
    do_return:
        cpu->pc = rpop(cpu);
        goto fetch_next;
    do_key: {
        if (cpu->input_pos < cpu->input_len) {
            push(cpu, (uint32_t)(unsigned char)cpu->input_buf[cpu->input_pos++]);
        } else {
            push(cpu, 0);
        }
        goto fetch_next;
    }
    do_rpush: {
        uint32_t val = pop(cpu);
        rpush(cpu, val);
        goto fetch_next;
    }
    do_rpop:
        push(cpu, rpop(cpu));
        goto fetch_next;
    do_rpeek:
        push(cpu, cpu->return_stack[cpu->rsp - 1]);
        goto fetch_next;
    do_sysenter: {
        cpu->csr[CSR_EPC] = cpu->pc - 1;
        cpu->csr[CSR_EDATA] = (cpu->dsp > 0) ? peek(cpu, 0) : 0;
        cpu->csr[CSR_STATUS] |= STATUS_KM;
        cpu->csr[CSR_STATUS] &= ~STATUS_UM;
        cpu->privilege = PRIV_KERNEL;
        if (cpu->csr[CSR_EVEC]) {
            cpu->pc = cpu->csr[CSR_EVEC] + 0x10;
        }
        goto fetch_next;
    }
    do_eret:
        cpu->csr[CSR_STATUS] &= ~STATUS_KM;
        cpu->csr[CSR_STATUS] |= STATUS_UM;
        cpu->privilege = PRIV_USER;
        cpu->pc = cpu->csr[CSR_EPC];
        goto fetch_next;
    do_csr_read: {
        uint32_t id = read_imm32(cpu, program, prog_len) & 0xFF;
        push(cpu, csr_read(cpu, id));
        goto fetch_next;
    }
    do_csr_write: {
        uint32_t id = read_imm32(cpu, program, prog_len) & 0xFF;
        uint32_t val = pop(cpu);
        csr_write(cpu, id, val);
        goto fetch_next;
    }
    do_tlb_flush:
        goto fetch_next;
    do_get_fp:
        push(cpu, cpu->fp);
        goto fetch_next;
    do_set_fp:
        cpu->fp = pop(cpu);
        goto fetch_next;

    /* ── Floating point helpers ────────────────────────────────────── */
    /* Double values occupy 2 stack slots: [hi, lo] (hi on top).
     * These macros pack/unpack double from 2 slots. */
#define POP_DOUBLE() ({ \
    uint32_t _lo = pop(cpu); \
    uint32_t _hi = pop(cpu); \
    double _v; *(uint32_t*)&_v = _lo; *((uint32_t*)&_v + 1) = _hi; _v; })
#define PUSH_DOUBLE(v) do { \
    double _v = (v); \
    push(cpu, *((uint32_t*)&_v + 1)); \
    push(cpu, *(uint32_t*)&_v); \
} while(0)

    do_fadd: { double a = POP_DOUBLE(); double b = POP_DOUBLE(); PUSH_DOUBLE(a + b); goto fetch_next; }
    do_fsub: { double a = POP_DOUBLE(); double b = POP_DOUBLE(); PUSH_DOUBLE(a - b); goto fetch_next; }
    do_fmul: { double a = POP_DOUBLE(); double b = POP_DOUBLE(); PUSH_DOUBLE(a * b); goto fetch_next; }
    do_fdiv: { double a = POP_DOUBLE(); double b = POP_DOUBLE(); PUSH_DOUBLE(a / b); goto fetch_next; }
    do_fcmp: {
        double a = POP_DOUBLE(); double b = POP_DOUBLE();
        push(cpu, (uint32_t)(int32_t)((a > b) ? 1 : (a < b) ? -1 : 0));
        goto fetch_next;
    }
    do_f2i:  { double v = POP_DOUBLE(); push(cpu, (uint32_t)(int32_t)v); goto fetch_next; }
    do_i2f:  { uint32_t v = pop(cpu); PUSH_DOUBLE((double)(int32_t)v); goto fetch_next; }
    do_call_ind: {
        uint32_t target = pop(cpu);
        rpush(cpu, cpu->pc);
        cpu->pc = target;
        goto fetch_next;
    }

    do_halt:
        cpu->halted = true;
        return 0;

    fetch_next:
        if (cpu->pc >= prog_len) return 0;
        if (cpu->max_steps > 0 && cpu->steps >= cpu->max_steps) return 0;
        opcode = program[cpu->pc++];
        cpu->steps++;
        trace_record(cpu, opcode);
        DISPATCH();
}

/* ── Initialization ────────────────────────────────────────────── */

static void cpu_init(cpu_t *cpu) {
    memset(cpu, 0, sizeof(*cpu));
    /* Start in kernel mode */
    cpu->csr[CSR_STATUS] = STATUS_KM;
    cpu->privilege = PRIV_KERNEL;
    /* UART TX ready by default */
    cpu->uart_status = 0x01;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    const char *filename = NULL;
    cpu_t cpu;
    int verbose = 0;
    int dump_ram = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dump") == 0) {
            dump_ram = 1;
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-steps") == 0) &&
                   i + 1 < argc) {
            cpu_init(&cpu);  /* need cpu to exist */
            cpu.max_steps = strtoul(argv[++i], NULL, 10);
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        } else {
            fprintf(stderr, "Usage: %s [-v] [-m max_steps] [-d] <binary.bin>\n", argv[0]);
            return 1;
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [-v] [-m max_steps] [-d] <binary.bin>\n", argv[0]);
        return 1;
    }

    /* Set stdout to binary mode on Windows (avoid \n → \r\n conversion) */
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    /* Load program */
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open '%s'\n", filename);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *program = malloc(file_size > 0 ? file_size : 1);
    if (file_size > 0) {
        size_t nread = fread(program, 1, file_size, f);
        if ((long)nread != file_size) {
            fprintf(stderr, "Short read on '%s'\n", filename);
            fclose(f);
            free(program);
            return 1;
        }
    }
    fclose(f);

    /* Detect and extract ELF executable .text segment */
    uint32_t entry_pc = 0;
    if (file_size >= (long)sizeof(Elf32_Ehdr) &&
        program[EI_MAG0] == ELFMAG0 && program[EI_MAG1] == ELFMAG1 &&
        program[EI_MAG2] == ELFMAG2 && program[EI_MAG3] == ELFMAG3) {
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *)program;
        if (ehdr->e_type == ET_EXEC && ehdr->e_phoff > 0 && ehdr->e_phnum > 0) {
            entry_pc = ehdr->e_entry;
            /* Find PT_LOAD segment */
            Elf32_Phdr *phdr = (Elf32_Phdr *)(program + ehdr->e_phoff);
            uint32_t text_off = 0, text_size = 0;
            for (int i = 0; i < ehdr->e_phnum; i++) {
                if (phdr[i].p_type == 1 /* PT_LOAD */) {
                    text_off = phdr[i].p_offset;
                    text_size = phdr[i].p_filesz;
                    break;
                }
            }
            if (text_size > 0 && text_off + text_size <= (uint32_t)file_size) {
                uint8_t *text = malloc(text_size);
                memcpy(text, program + text_off, text_size);
                free(program);
                program = text;
                file_size = (long)text_size;
            }
        }
    }

    fprintf(stderr, "Loaded %ld bytes from %s\n", file_size, filename);

    /* Initialize CPU */
    cpu_init(&cpu);
    cpu.trace = verbose;
    if (cpu.max_steps == 0) {
        /* not set via -m; default unlimited */
        cpu.max_steps = 0;
    }

    /* Read all of stdin into input buffer for KEY opcode */
    {
        size_t cap = 4096, len = 0;
        char *buf = malloc(cap);
        size_t n;
        while ((n = fread(buf + len, 1, cap - len, stdin)) > 0) {
            len += n;
            if (len >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
        }
        cpu.input_buf = buf;
        cpu.input_pos = 0;
        cpu.input_len = len;
    }

    /* Run */
    cpu.pc = entry_pc;
    execute(&cpu, program, (size_t)file_size);

    /* Report results */
    if (verbose || !cpu.halted) {
        fprintf(stderr, "\n=== Execution finished after %llu steps ===\n",
                (unsigned long long)cpu.steps);
        fprintf(stderr, "Output: ");
        /* Print output as string */
        fputc('\"', stderr);
        for (size_t i = 0; i < cpu.output_len; i++) {
            unsigned char c = (unsigned char)cpu.output[i];
            if (c == '\n') fprintf(stderr, "\\n");
            else if (c == '\r') fprintf(stderr, "\\r");
            else if (c == '\t') fprintf(stderr, "\\t");
            else if (c >= 32 && c < 127) fputc(c, stderr);
            else fprintf(stderr, "\\x%02x", c);
        }
        fputc('\"', stderr);
        fputc('\n', stderr);

        if (cpu.dsp > 0) {
            fprintf(stderr, "Final data stack: [");
            for (uint32_t i = 0; i < cpu.dsp; i++) {
                if (i > 0) fprintf(stderr, ", ");
                fprintf(stderr, "0x%08X", cpu.data_stack[i]);
            }
            fprintf(stderr, "]\n");
        }
        if (cpu.rsp > 0) {
            fprintf(stderr, "Final return stack: [");
            for (uint32_t i = 0; i < cpu.rsp; i++) {
                if (i > 0) fprintf(stderr, ", ");
                fprintf(stderr, "0x%08X", cpu.return_stack[i]);
            }
            fprintf(stderr, "]\n");
        }
    }

    /* Print captured output to stdout (matching sim.py behavior) */
    fwrite(cpu.output, 1, cpu.output_len, stdout);

    if (dump_ram) {
        fprintf(stderr, "\nRAM dump (words 0-31):\n");
        for (int i = 0; i < 32; i++) {
            fprintf(stderr, "  [%4d] 0x%08X\n", i, cpu.ram[i]);
        }
    }

    /* Dump OLED framebuffer (1024 bytes starting at byte-address 0x1000) */
    {
        unsigned long fb_start = 0x1000 / 4;
        fwrite(&cpu.ram[fb_start], 1, 1024, stdout);
        fflush(stdout);
    }

    free(program);
    /* Return top of data stack as exit code (like C's main returning int) */
    if (cpu.dsp > 0)
        return (int)(cpu.data_stack[cpu.dsp - 1] & 0xFF);
    return 0;
}
