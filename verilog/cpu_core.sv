`default_nettype none

module cpu_core (
    input  logic       clk,
    input  logic       rst_n,

    // Program BRAM interface (byte-addressable)
    output logic [13:0] prog_addr,
    input  logic [ 7:0] prog_rdata,
    output logic        prog_re,

    // Data BRAM interface (32-bit word, 4-byte enables)
    output logic [27:0] data_addr,
    output logic [31:0] data_din,
    input  logic [31:0] data_dout,
    output logic        data_we,
    output logic [ 3:0] data_be,

    // UART
    output logic [ 7:0] uart_tx_data,
    output logic        uart_tx_send,
    input  logic        uart_tx_busy,
    input  logic [ 7:0] uart_rx_data,
    input  logic        uart_rx_ready,
    output logic        uart_rx_ack,

    // Status
    output logic [5:0]  led,
    output logic        halted,

    // Button input
    input  logic [1:0]  btn,

    // Boot done signal (from boot_loader)
    input  logic        boot_done
);

    // ── Opcode constants (matching sim_s32.c) ──────────────────
    localparam OP_PUSH     = 8'h01;
    localparam OP_ADD      = 8'h02;
    localparam OP_SUB      = 8'h03;
    localparam OP_MUL      = 8'h04;
    localparam OP_DROP     = 8'h05;
    localparam OP_PRINT    = 8'h08;
    localparam OP_EQ       = 8'h09;
    localparam OP_LT_S     = 8'h0A;
    localparam OP_GT_S     = 8'h0B;
    localparam OP_LT_U     = 8'h0C;
    localparam OP_GT_U     = 8'h0D;
    localparam OP_BR_IF    = 8'h0E;
    localparam OP_JUMP     = 8'h0F;
    localparam OP_CALL     = 8'h10;
    localparam OP_RETURN   = 8'h11;
    localparam OP_DUP      = 8'h12;
    localparam OP_SWAP     = 8'h13;
    localparam OP_OVER     = 8'h14;
    localparam OP_ROT      = 8'h15;
    localparam OP_AND      = 8'h16;
    localparam OP_OR       = 8'h17;
    localparam OP_XOR      = 8'h18;
    localparam OP_NOT      = 8'h19;
    localparam OP_SHL      = 8'h1A;
    localparam OP_SHR_U    = 8'h1B;
    localparam OP_SHR_S    = 8'h1C;
    localparam OP_LOAD     = 8'h1D;
    localparam OP_STORE    = 8'h1E;
    localparam OP_KEY      = 8'h1F;
    localparam OP_TO_R     = 8'h30;
    localparam OP_FROM_R   = 8'h31;
    localparam OP_R_FETCH  = 8'h32;
    localparam OP_DEPTH    = 8'h33;
    localparam OP_R_DEPTH  = 8'h34;
    localparam OP_EQZ      = 8'h35;
    localparam OP_DIV_S    = 8'h36;
    localparam OP_LOAD8_U  = 8'h37;
    localparam OP_STORE8   = 8'h38;
    localparam OP_LOCAL_GET = 8'h39;
    localparam OP_LOCAL_SET = 8'h3A;
    localparam OP_GET_FP   = 8'h40;
    localparam OP_SET_FP   = 8'h49;
    localparam OP_HALT     = 8'hFF;

    // ── MMIO address decoding ─────────────────────────────────
    localparam MMIO_PAGE_UART = 8'hFE;
    localparam MMIO_PAGE_IO   = 8'hFF;

    function is_mmio_page;
        input [27:0] a;
        is_mmio_page = (a[27:20] == MMIO_PAGE_UART || a[27:20] == MMIO_PAGE_IO);
    endfunction

    // ── Stack parameters ──────────────────────────────────────
    localparam DSP_DEPTH = 64;
    localparam DSP_BITS  = 6;
    localparam RSP_DEPTH = 32;
    localparam RSP_BITS  = 5;

    // ── State machine ─────────────────────────────────────────
    typedef enum logic [4:0] {
        ST_RESET,
        ST_FETCH,
        ST_DECODE,
        ST_FETCH_IMM,
        ST_FETCH_IMM2,
        ST_EXECUTE,
        ST_DIV_LOOP,
        ST_DIV_DONE,
        ST_MEM_READ,
        ST_MEM_READ2,
        ST_MEM_WRITE,
        ST_UART_TX_WAIT,
        ST_UART_RX_WAIT,
        ST_STK_READ,
        ST_STK_READ2,
        ST_HALTED
    } state_t;

    state_t state = ST_RESET;

    // ── PC and instruction state ──────────────────────────────
    reg [13:0] pc = 0;
    reg [13:0] pc_next = 0;
    reg [ 7:0] opcode = 0;
    reg [31:0] imm32 = 0;
    reg [ 2:0] imm_need = 0;
    reg [ 2:0] imm_got  = 0;

    // ── Data stack core registers ─────────────────────────────
    reg [DSP_BITS-1:0] dsp = 0;
    reg [31:0] tos = 0;
    reg [31:0] nos = 0;
    reg [DSP_BITS-1:0] dsp_prev = 0;

    // ── Data stack BRAM (write-side) ──────────────────────────
    reg [31:0] dstack [0:DSP_DEPTH-1];
    reg        dstack_we = 0;
    reg [DSP_BITS-1:0] dstack_waddr = 0;
    reg [31:0] dstack_wdata = 0;

    // ── Data stack BRAM (read-side) ───────────────────────────
    reg [DSP_BITS-1:0] dstack_raddr = 0;
    reg [31:0] dstack_rdata = 0;

    // ── Return stack ──────────────────────────────────────────
    reg [RSP_BITS-1:0] rsp = 0;
    reg [31:0] rstack [0:RSP_DEPTH-1];
    reg        rstack_we = 0;
    reg [RSP_BITS-1:0] rstack_waddr = 0;
    reg [31:0] rstack_wdata = 0;
    reg [RSP_BITS-1:0] rstack_raddr = 0;
    reg [31:0] rstack_rdata = 0;

    // ── Frame pointer ─────────────────────────────────────────
    reg [31:0] fp = 0;

    // ── Division state ────────────────────────────────────────
    reg [31:0] div_a = 0;
    reg [31:0] div_b = 0;
    reg [31:0] div_q = 0;
    reg [31:0] div_r = 0;
    reg [5:0]  div_i = 0;

    // ── Timer ─────────────────────────────────────────────────
    reg [31:0] timer_cnt = 0;
    reg [31:0] timer_cmp = 0;
    reg [ 1:0] timer_ctl = 0;

    // ── BRAM read port for data stack ─────────────────────────
    always @(posedge clk) begin
        dstack_rdata <= dstack[dstack_raddr];
    end

    always @(posedge clk) begin
        if (dstack_we)
            dstack[dstack_waddr] <= dstack_wdata;
    end

    // ── BRAM read port for return stack ───────────────────────
    always @(posedge clk) begin
        rstack_rdata <= rstack[rstack_raddr];
    end

    always @(posedge clk) begin
        if (rstack_we)
            rstack[rstack_waddr] <= rstack_wdata;
    end

    // ── MMIO read (combinational) ─────────────────────────────
    function automatic [31:0] mmio_read;
        input [27:0] a;
        case (a[15:0])
            16'h0000: mmio_read = {24'b0, uart_rx_data};
            16'h0004: mmio_read = {30'b0, uart_rx_ready, ~uart_tx_busy};
            16'h1000: mmio_read = timer_cnt;
            16'h1004: mmio_read = timer_cmp;
            16'h1008: mmio_read = {30'b0, timer_ctl};
            default: begin
                casez (a[15:0])
                    16'hF000: mmio_read = {24'b0, ~led};
                    16'hF004: mmio_read = {30'b0, btn};
                    default:  mmio_read = 0;
                endcase
            end
        endcase
    endfunction

    // ── MMIO write (decodes on posedge) ───────────────────────
    task automatic mmio_write;
        input [27:0] a;
        input [31:0] v;
        case (a[15:0])
            16'h0000: begin uart_tx_data <= v[7:0]; uart_tx_send <= 1; end
            16'h1004: timer_cmp <= v;
            16'h1008: timer_ctl <= v[1:0];
            default: begin
                casez (a[15:0])
                    16'hF000: led <= ~v[5:0];
                endcase
            end
        endcase
    endtask

    // ── Stack push (combinational control) ──────────────────
    wire stack_ok = (dsp < DSP_DEPTH);
    wire rstack_ok = (rsp < RSP_DEPTH);
    wire stack_has  = (dsp > 0);
    wire stack_has2 = (dsp > 1);
    wire stack_has3 = (dsp > 2);
    wire rstack_has = (rsp > 0);

    // ── Main FSM ──────────────────────────────────────────────
    always @(posedge clk) begin
        if (!rst_n || !boot_done) begin
            state <= ST_RESET;
            pc <= 0;
            dsp <= 0;
            rsp <= 0;
            fp <= 0;
            halted <= 0;
            led <= 6'b111111;
            uart_tx_send <= 0;
            uart_rx_ack <= 0;
            data_we <= 0;
            data_be <= 4'b1111;
            prog_re <= 0;
            dstack_we <= 0;
            rstack_we <= 0;
            imm32 <= 0;
            timer_cnt <= 0;
        end else begin
            // clear one-shot strobes
            uart_tx_send <= 0;
            uart_rx_ack <= 0;
            data_we <= 0;
            dstack_we <= 0;
            rstack_we <= 0;
            prog_re <= 0;
            dsp_prev <= dsp;

            // timer
            if (timer_ctl[0]) timer_cnt <= timer_cnt + 1;

            case (state)
                // ── RESET: wait for boot ─────────────────────
                ST_RESET: begin
                    pc <= 0;
                    dsp <= 0;
                    rsp <= 0;
                    if (boot_done)
                        state <= ST_FETCH;
                end

                // ── FETCH: request byte from prog BRAM ────────
                ST_FETCH: begin
                    prog_addr <= pc[13:0];
                    prog_re <= 1;
                    pc_next <= pc + 1;
                    // Refill NOS from BRAM after a pop (when dsp decreased)
                    if (rst_n && boot_done && dsp < dsp_prev && dsp >= 2)
                        nos <= dstack_rdata;
                    state <= ST_DECODE;
                end

                // ── DECODE: capture opcode, plan imm count ────
                ST_DECODE: begin
                    opcode <= prog_rdata;
                    pc <= pc_next;
                    unique case (prog_rdata)
                        OP_PUSH, OP_BR_IF, OP_JUMP, OP_CALL: begin
                            imm_need <= 4;
                            imm_got  <= 0;
                            imm32 <= 0;
                            state <= ST_FETCH_IMM;
                        end
                        OP_LOCAL_GET, OP_LOCAL_SET: begin
                            imm_need <= 1;
                            imm_got  <= 0;
                            imm32 <= 0;
                            state <= ST_FETCH_IMM;
                        end
                        OP_HALT: state <= ST_HALTED;
                        default: state <= ST_EXECUTE;
                    endcase
                end

                // ── FETCH_IMM: read immediate bytes ───────────
                ST_FETCH_IMM: begin
                    prog_addr <= pc[13:0];
                    prog_re <= 1;
                    state <= ST_FETCH_IMM2;
                end

                ST_FETCH_IMM2: begin
                    imm32 <= imm32 | (prog_rdata << (imm_got * 8));
                    pc <= pc + 1;
                    if (imm_got + 1 >= imm_need)
                        state <= ST_EXECUTE;
                    else begin
                        imm_got <= imm_got + 1;
                        state <= ST_FETCH_IMM;
                    end
                end

                // ── EXECUTE ───────────────────────────────────
                ST_EXECUTE: begin
                    unique case (opcode)
                        // ── PUSH immediate ────────────────────
                        OP_PUSH: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                tos <= imm32;
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        // ── ALU (pop 2, push 1) ──────────────
                        OP_ADD: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos + tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                // nos will be filled from BRAM in FETCH
                                state <= ST_FETCH;
                            end
                        end

                        OP_SUB: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos - tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_MUL: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos * tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_DIV_S: begin
                            if (!stack_has2) begin halted <= 1; end
                            else if (tos == 0) begin
                                tos <= 0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end else begin
                                div_a <= nos;
                                div_b <= tos;
                                div_q <= 0;
                                div_r <= 0;
                                div_i <= 0;
                                state <= ST_DIV_LOOP;
                            end
                        end

                        // ── Comparisons ───────────────────────
                        OP_EQ: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= (nos == tos) ? 32'd1 : 32'd0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_LT_S: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= ($signed(nos) < $signed(tos)) ? 32'd1 : 32'd0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_GT_S: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= ($signed(nos) > $signed(tos)) ? 32'd1 : 32'd0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_LT_U: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= (nos < tos) ? 32'd1 : 32'd0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_GT_U: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= (nos > tos) ? 32'd1 : 32'd0;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_EQZ: begin
                            tos <= (tos == 0) ? 32'd1 : 32'd0;
                            state <= ST_FETCH;
                        end

                        // ── Bitwise ───────────────────────────
                        OP_AND: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos & tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_OR: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos | tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_XOR: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos ^ tos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_NOT: begin
                            tos <= ~tos;
                            state <= ST_FETCH;
                        end

                        OP_SHL: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos << tos[4:0];
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_SHR_U: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= nos >> tos[4:0];
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_SHR_S: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                tos <= $signed(nos) >>> tos[4:0];
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        // ── Stack manipulation ────────────────
                        OP_DROP: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_FETCH;
                            end
                        end

                        OP_DUP: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_SWAP: begin
                            if (!stack_has2) begin halted <= 1; end
                            else {tos, nos} <= {nos, tos};
                            state <= ST_FETCH;
                        end

                        OP_OVER: begin
                            if (!stack_ok || !stack_has2) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                tos <= nos;
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_ROT: begin
                            if (!stack_has3) begin halted <= 1; end
                            else begin
                                // Need to read third element from BRAM
                                dstack_raddr <= dsp - 3;
                                state <= ST_STK_READ;
                            end
                        end

                        OP_DEPTH: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                tos <= {26'b0, dsp};
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_R_DEPTH: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                tos <= {27'b0, rsp};
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        // ── Return stack ──────────────────────
                        OP_TO_R: begin
                            if (!stack_has) begin halted <= 1; end
                            else if (!rstack_ok) begin halted <= 1; end
                            else begin
                                rstack_we <= 1;
                                rstack_waddr <= rsp;
                                rstack_wdata <= tos;
                                rsp <= rsp + 1;
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_FROM_R: begin
                            if (!rstack_has) begin halted <= 1; end
                            else if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                rsp <= rsp - 1;
                                rstack_raddr <= rsp - 1;
                                state <= ST_STK_READ2;
                            end
                        end

                        OP_R_FETCH: begin
                            if (!rstack_has) begin
                                if (!stack_ok) begin halted <= 1; end
                                else begin
                                    dstack_we <= 1;
                                    dstack_waddr <= dsp;
                                    dstack_wdata <= tos;
                                    nos <= tos;
                                    tos <= 0;
                                    dsp <= dsp + 1;
                                end
                            end else begin
                                if (!stack_ok) begin halted <= 1; end
                                else begin
                                    dstack_we <= 1;
                                    dstack_waddr <= dsp;
                                    dstack_wdata <= tos;
                                    nos <= tos;
                                    tos <= rstack[rsp-1];
                                    dsp <= dsp + 1;
                                end
                            end
                            state <= ST_FETCH;
                        end

                        // ── Control flow ──────────────────────
                        OP_BR_IF: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                if (tos != 0) pc <= imm32[13:0];
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_JUMP: begin
                            pc <= imm32[13:0];
                            state <= ST_FETCH;
                        end

                        OP_CALL: begin
                            if (!rstack_ok) begin halted <= 1; end
                            else begin
                                rstack_we <= 1;
                                rstack_waddr <= rsp;
                                rstack_wdata <= pc;
                                rsp <= rsp + 1;
                                pc <= imm32[13:0];
                            end
                            state <= ST_FETCH;
                        end

                        OP_RETURN: begin
                            if (!rstack_has) begin halted <= 1; end
                            else begin
                                rsp <= rsp - 1;
                                rstack_raddr <= rsp - 1;
                                state <= ST_STK_READ2;
                            end
                        end

                        // ── Memory ────────────────────────────
                        OP_LOAD: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                data_addr <= tos;
                                data_be <= 4'b1111;
                                data_we <= 0;
                                state <= ST_MEM_READ;
                            end
                        end

                        OP_STORE: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                data_addr <= tos;
                                data_din <= nos;
                                data_be <= 4'b1111;
                                data_we <= 1;
                                tos <= nos;
                                if (dsp > 3) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_MEM_WRITE;
                            end
                        end

                        OP_LOAD8_U: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                data_addr <= tos;
                                data_be <= 4'b1111;
                                data_we <= 0;
                                state <= ST_MEM_READ;
                            end
                        end

                        OP_STORE8: begin
                            if (!stack_has2) begin halted <= 1; end
                            else begin
                                data_addr <= tos;
                                data_be <= 4'b0001;
                                data_din <= {24'b0, nos[7:0]};
                                data_we <= 1;
                                tos <= nos;
                                if (dsp > 3) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                                state <= ST_MEM_WRITE;
                            end
                        end

                        // ── Frame pointer ─────────────────────
                        OP_LOCAL_GET: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                data_addr <= (fp + imm32[5:0]);
                                data_be <= 4'b1111;
                                data_we <= 0;
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                dsp <= dsp + 1;
                                state <= ST_MEM_READ;
                            end
                        end

                        OP_LOCAL_SET: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                data_addr <= (fp + imm32[5:0]);
                                data_din <= tos;
                                data_be <= 4'b1111;
                                data_we <= 1;
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_GET_FP: begin
                            if (!stack_ok) begin halted <= 1; end
                            else begin
                                dstack_we <= 1;
                                dstack_waddr <= dsp;
                                dstack_wdata <= tos;
                                nos <= tos;
                                tos <= fp;
                                dsp <= dsp + 1;
                            end
                            state <= ST_FETCH;
                        end

                        OP_SET_FP: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                fp <= tos;
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                            end
                            state <= ST_FETCH;
                        end

                        // ── I/O ──────────────────────────────
                        OP_PRINT: begin
                            if (!stack_has) begin halted <= 1; end
                            else begin
                                uart_tx_data <= tos[7:0];
                                uart_tx_send <= 1;
                                tos <= nos;
                                if (dsp > 2) dstack_raddr <= dsp - 3;
                                dsp <= dsp - 1;
                            end
                            state <= ST_UART_TX_WAIT;
                        end

                        OP_KEY: begin
                            if (uart_rx_ready) begin
                                if (!stack_ok) begin halted <= 1; end
                                else begin
                                    dstack_we <= 1;
                                    dstack_waddr <= dsp;
                                    dstack_wdata <= tos;
                                    nos <= tos;
                                    tos <= {24'b0, uart_rx_data};
                                    dsp <= dsp + 1;
                                    uart_rx_ack <= 1;
                                end
                                state <= ST_UART_RX_WAIT;
                            end else begin
                                state <= ST_EXECUTE;
                            end
                        end

                        default: state <= ST_FETCH;
                    endcase
                end

                // ── Division ─────────────────────────────────
                ST_DIV_LOOP: begin
                    if (div_i < 32) begin
                        div_r <= (div_r << 1) | (div_a >> 31);
                        div_a <= div_a << 1;
                        if ($signed(div_r) >= 0) begin
                            div_r <= $signed(div_r) - $signed(div_b);
                            div_q <= (div_q << 1) | 1;
                        end else begin
                            div_r <= $signed(div_r) + $signed(div_b);
                            div_q <= (div_q << 1);
                        end
                        div_i <= div_i + 1;
                    end else begin
                        state <= ST_DIV_DONE;
                    end
                end

                ST_DIV_DONE: begin
                    tos <= div_q;
                    if (dsp > 2) dstack_raddr <= dsp - 3;
                    dsp <= dsp - 1;
                    state <= ST_FETCH;
                end

                // ── Memory wait states ───────────────────────
                ST_MEM_READ: begin
                    state <= ST_MEM_READ2;
                end

                ST_MEM_READ2: begin
                    if (is_mmio_page(data_addr[27:0])) begin
                        tos <= mmio_read(data_addr[27:0]);
                    end else begin
                        tos <= data_dout;
                        if (opcode == OP_LOAD8_U)
                            tos <= {24'b0, data_dout[7:0]};
                    end
                    state <= ST_FETCH;
                end

                ST_MEM_WRITE: begin
                    if (is_mmio_page(data_addr[27:0])) begin
                        mmio_write(data_addr[27:0], data_din);
                    end
                    state <= ST_FETCH;
                end

                // ── Stack read wait states ────────────────────
                ST_STK_READ: begin
                    // ROT: data from dstack_raddr in dstack_rdata
                    // Stack: (tos=c, nos=b, third=a)
                    // Result: (tos=b, nos=a, old c pushed)
                    dstack_we <= 1;
                    dstack_waddr <= dsp;
                    dstack_wdata <= tos;
                    // Rotate: new tos=nos, new nos=dstack_rdata
                    tos <= nos;
                    nos <= dstack_rdata;
                    dsp <= dsp + 1;
                    state <= ST_FETCH;
                end

                ST_STK_READ2: begin
                    // FROM_R: push rstack_rdata to data stack
                    // RETURN: restore PC from rstack_rdata
                    if (opcode == OP_FROM_R) begin
                        dstack_we <= 1;
                        dstack_waddr <= dsp;
                        dstack_wdata <= tos;
                        nos <= tos;
                        tos <= rstack_rdata;
                        dsp <= dsp + 1;
                    end else if (opcode == OP_RETURN) begin
                        pc <= rstack_rdata;
                    end
                    state <= ST_FETCH;
                end

                // ── UART wait ─────────────────────────────────
                ST_UART_TX_WAIT: begin
                    if (!uart_tx_busy) state <= ST_FETCH;
                end

                ST_UART_RX_WAIT: begin
                    if (!uart_rx_ready) begin
                        uart_rx_ack <= 0;
                        state <= ST_FETCH;
                    end
                end

                // ── HALTED ────────────────────────────────────
                ST_HALTED: begin
                    halted <= 1;
                    led <= 6'b000000;
                end

                default: state <= ST_FETCH;
            endcase
        end
    end

    // ── NOS refill on FETCH ────────────────────────────────────
    // After a pop that leaves dsp >= 2, the new NOS comes from the
    // BRAM read that was initiated in the EXECUTE state.
    // Handled inside the main FSM's ST_FETCH case to avoid multi-driver issues.

endmodule