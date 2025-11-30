`default_nettype none

module stack_cpu
#(
    parameter STACK_DEPTH_LOG2 = 3  // 3 = 8 entries - maximum for Tang Nano 9K with full ISA
)
(
    input clk,
    input reset,
    
    // Flash interface
    output reg [23:0] flash_addr = 0,
    input [7:0] flash_data,
    output reg flash_enable = 0,
    input flash_ready,
    
    // UART interface
    output reg [7:0] uart_data = 0,
    output reg uart_send = 0,
    input uart_busy,
    input [7:0] uart_rx_data,
    input uart_rx_ready,
    output reg uart_rx_ack = 0,
    
    // RAM interface
    output reg ram_we = 0,
    output reg [9:0] ram_addr = 0,
    output reg [31:0] ram_din = 0,
    input [31:0] ram_dout,
    
    // Status outputs
    output reg [5:0] status_led = 6'b111111, // Active low
    output reg halted = 0
);

    // ISA Opcodes (WASM subset)
    localparam OP_PUSH     = 8'h01;  // Push constant: PUSH <imm32>
    localparam OP_ADD      = 8'h02;  // i32.add: pop 2, push sum
    localparam OP_SUB      = 8'h03;  // i32.sub: pop 2, push diff
    localparam OP_MUL      = 8'h04;  // i32.mul: pop 2, push product
    localparam OP_DROP     = 8'h05;  // drop: pop 1
    localparam OP_PRINT    = 8'h08;  // print: pop and send over UART as ASCII
    
    // Comparison operations
    localparam OP_EQ       = 8'h09;  // i32.eq: pop 2, push (a == b)
    localparam OP_LT_S     = 8'h0A;  // i32.lt_s: pop 2, push (a < b) signed
    localparam OP_GT_S     = 8'h0B;  // i32.gt_s: pop 2, push (a > b) signed
    localparam OP_LT_U     = 8'h0C;  // i32.lt_u: pop 2, push (a < b) unsigned
    localparam OP_GT_U     = 8'h0D;  // i32.gt_u: pop 2, push (a > b) unsigned
    
    // Control flow
    localparam OP_BR_IF    = 8'h0E;  // br_if <offset>: conditional branch
    localparam OP_JUMP     = 8'h0F;  // jump <addr>: unconditional jump
    localparam OP_CALL     = 8'h10;  // call <addr>: call subroutine
    localparam OP_RETURN   = 8'h11;  // return: return from subroutine
    
    // Stack operations
    localparam OP_DUP      = 8'h12;  // dup: duplicate top of stack
    localparam OP_SWAP     = 8'h13;  // swap: swap top two stack items
    localparam OP_OVER     = 8'h14;  // over: copy second item to top
    localparam OP_ROT      = 8'h15;  // rot: rotate top 3 items
    
    // Bitwise operations
    localparam OP_AND      = 8'h16;  // i32.and: bitwise AND
    localparam OP_OR       = 8'h17;  // i32.or: bitwise OR
    localparam OP_XOR      = 8'h18;  // i32.xor: bitwise XOR
    localparam OP_NOT      = 8'h19;  // i32.not: bitwise NOT
    localparam OP_SHL      = 8'h1A;  // i32.shl: shift left
    localparam OP_SHR_U    = 8'h1B;  // i32.shr_u: shift right unsigned
    localparam OP_SHR_S    = 8'h1C;  // i32.shr_s: shift right signed
    
    // Memory operations
    localparam OP_LOAD     = 8'h1D;  // load: pop addr, push value
    localparam OP_STORE    = 8'h1E;  // store: pop addr, pop value, store
    
    // I/O operations
    localparam OP_KEY      = 8'h1F;  // key: wait for and push UART char
    
    // Return stack operations (Forth support)
    localparam OP_TO_R     = 8'h30;  // >r: move top of data stack to return stack
    localparam OP_FROM_R   = 8'h31;  // r>: move top of return stack to data stack
    localparam OP_R_FETCH  = 8'h32;  // r@: copy top of return stack to data stack
    localparam OP_DEPTH    = 8'h33;  // depth: push stack depth
    localparam OP_R_DEPTH  = 8'h34;  // rdepth: push return stack depth
    localparam OP_ZEQ      = 8'h35;  // eqz: pop 1, push (a == 0)

    localparam OP_HALT     = 8'hFF;  // halt execution

    // CPU states
    localparam STATE_FETCH           = 4'd0;
    localparam STATE_FETCH_WAIT_LOW  = 4'd1;
    localparam STATE_FETCH_WAIT_HIGH = 4'd2;
    localparam STATE_DECODE          = 4'd3;
    localparam STATE_FETCH_IMM       = 4'd4;
    localparam STATE_FETCH_IMM_WAIT_LOW  = 4'd5;
    localparam STATE_FETCH_IMM_WAIT_HIGH = 4'd6;
    localparam STATE_EXECUTE         = 4'd7;
    localparam STATE_UART_WAIT       = 4'd8;
    localparam STATE_HALT            = 4'd9;
    localparam STATE_KEY_WAIT        = 4'd10;
    localparam STATE_RAM_WAIT        = 4'd11;
    localparam STATE_SWAP_WAIT       = 4'd12;
    localparam STATE_RAM_READ        = 4'd13;
    localparam STATE_ALU_WAIT        = 4'd14;

    reg [3:0] state = STATE_FETCH;
    reg [23:0] pc = 0;
    reg [7:0] opcode = 0;
    
    // Parameterized stacks - 8 entries max for Tang Nano 9K
    localparam STACK_SIZE = 2 ** STACK_DEPTH_LOG2;
    
    // Data stack - distributed RAM (registers)
    reg [31:0] stack [0:STACK_SIZE-1];
    reg [STACK_DEPTH_LOG2-1:0] sp = 0;
    
    // Return stack - distributed RAM (registers)
    reg [31:0] rstack [0:STACK_SIZE-1];
    reg [STACK_DEPTH_LOG2-1:0] rsp = 0;
    
    // Immediate value buffer (32-bit)
    reg [31:0] imm32 = 0;
    reg [1:0] imm_byte_count = 0;
    
    // Temporary register for SWAP operation
    reg [31:0] temp_swap = 0;
    reg [31:0] temp_alu = 0;

    // Main state machine
    always @(posedge clk) begin
        if (reset) begin
            state <= STATE_FETCH;
            pc <= 0;
            sp <= 0;
            rsp <= 0;
            halted <= 0;
            status_led <= 6'b111111;
            uart_send <= 0;
            uart_rx_ack <= 0;
            flash_enable <= 0;
            ram_we <= 0;
            imm32 <= 0;
            imm_byte_count <= 0;
            temp_swap <= 0;
        end else begin
            case (state)
                STATE_FETCH: begin
                    if (~flash_enable) begin
                        flash_addr <= pc;
                        flash_enable <= 1;
                        state <= STATE_FETCH_WAIT_LOW;
                    end
                end

                STATE_FETCH_WAIT_LOW: begin
                    if (~flash_ready) begin
                        state <= STATE_FETCH_WAIT_HIGH;
                    end
                end

                STATE_FETCH_WAIT_HIGH: begin
                    if (flash_ready) begin
                        opcode <= flash_data;
                        flash_enable <= 0;
                        status_led <= ~flash_data[5:0];
                        state <= STATE_DECODE;
                    end
                end

                STATE_DECODE: begin
                    pc <= pc + 1;
                    case (opcode)
                        OP_PUSH: begin
                            imm32 <= 0;
                            imm_byte_count <= 0;
                            state <= STATE_FETCH_IMM;
                        end
                        
                        OP_BR_IF, OP_JUMP, OP_CALL: begin
                            imm32 <= 0;
                            imm_byte_count <= 0;
                            state <= STATE_FETCH_IMM;
                        end
                        
                        default: begin
                            state <= STATE_EXECUTE;
                        end
                    endcase
                end

                STATE_FETCH_IMM: begin
                    if (~flash_enable) begin
                        flash_addr <= pc;
                        flash_enable <= 1;
                        state <= STATE_FETCH_IMM_WAIT_LOW;
                    end
                end

                STATE_FETCH_IMM_WAIT_LOW: begin
                    if (~flash_ready) begin
                        state <= STATE_FETCH_IMM_WAIT_HIGH;
                    end
                end

                STATE_FETCH_IMM_WAIT_HIGH: begin
                    if (flash_ready) begin
                        flash_enable <= 0;
                        pc <= pc + 1;
                        
                        if (opcode == OP_PUSH) begin
                            imm32 <= imm32 | (flash_data << (imm_byte_count * 8));
                            
                            if (imm_byte_count == 3) begin
                                state <= STATE_EXECUTE;
                            end else begin
                                imm_byte_count <= imm_byte_count + 1;
                                state <= STATE_FETCH_IMM;
                            end
                        end else if (opcode == OP_BR_IF || opcode == OP_JUMP || opcode == OP_CALL) begin
                            imm32 <= imm32 | (flash_data << (imm_byte_count * 8));
                            
                            if (imm_byte_count == 3) begin
                                state <= STATE_EXECUTE;
                            end else begin
                                imm_byte_count <= imm_byte_count + 1;
                                state <= STATE_FETCH_IMM;
                            end
                        end else begin
                            // local.get / local.set
                            imm32 <= {24'd0, flash_data};
                            state <= STATE_EXECUTE;
                        end
                    end
                end

                STATE_EXECUTE: begin
                    status_led <= 6'b111011; // LED2 on during execute
                    case (opcode)
                        OP_PUSH: begin
                            stack[sp] <= imm32;
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_ADD: begin
                            stack[sp-2] <= stack[sp-2] + stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_SUB: begin
                            stack[sp-2] <= stack[sp-2] - stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_MUL: begin
                            stack[sp-2] <= stack[sp-2] * stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_DROP: begin
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_PRINT: begin
                            uart_data <= stack[sp-1][7:0];
                            sp <= sp - 1;
                            uart_send <= 1;
                            state <= STATE_UART_WAIT;
                        end

                        OP_EQ: begin
                            temp_alu <= (stack[sp-2] == stack[sp-1]) ? 32'd1 : 32'd0;
                            sp <= sp - 1;
                            state <= STATE_ALU_WAIT;
                        end

                        OP_LT_S: begin
                            temp_alu <= ($signed(stack[sp-2]) < $signed(stack[sp-1])) ? 32'd1 : 32'd0;
                            sp <= sp - 1;
                            state <= STATE_ALU_WAIT;
                        end

                        OP_LT_U: begin
                            temp_alu <= (stack[sp-2] < stack[sp-1]) ? 32'd1 : 32'd0;
                            sp <= sp - 1;
                            state <= STATE_ALU_WAIT;
                        end

                        OP_GT_U: begin
                            temp_alu <= (stack[sp-2] > stack[sp-1]) ? 32'd1 : 32'd0;
                            sp <= sp - 1;
                            state <= STATE_ALU_WAIT;
                        end

                        OP_GT_U: begin
                            temp_alu <= (stack[sp-2] > stack[sp-1]) ? 32'd1 : 32'd0;
                            sp <= sp - 1;
                            state <= STATE_ALU_WAIT;
                            state <= STATE_FETCH;
                        end

                        OP_BR_IF: begin
                            if (stack[sp-1] != 0) begin
                                pc <= imm32[23:0];
                            end
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_JUMP: begin
                            pc <= imm32[23:0];
                            state <= STATE_FETCH;
                        end

                        OP_CALL: begin
                            rstack[rsp] <= {8'd0, pc};
                            rsp <= rsp + 1;
                            pc <= imm32[23:0];
                            state <= STATE_FETCH;
                        end

                        OP_RETURN: begin
                            pc <= rstack[rsp-1][23:0];
                            rsp <= rsp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_TO_R: begin
                            rstack[rsp] <= stack[sp-1];
                            rsp <= rsp + 1;
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_FROM_R: begin
                            stack[sp] <= rstack[rsp-1];
                            sp <= sp + 1;
                            rsp <= rsp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_R_FETCH: begin
                            stack[sp] <= rstack[rsp-1];
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_DEPTH: begin
                            stack[sp] <= {{(32-STACK_DEPTH_LOG2){1'b0}}, sp};
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_R_DEPTH: begin
                            stack[sp] <= {{(32-STACK_DEPTH_LOG2){1'b0}}, rsp};
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_ZEQ: begin
                            stack[sp-1] <= (stack[sp-1] == 0) ? 32'd1 : 32'd0;
                            state <= STATE_FETCH;
                        end

                        OP_DUP: begin
                            stack[sp] <= stack[sp-1];
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_SWAP: begin
                            temp_swap <= stack[sp-1];
                            stack[sp-1] <= stack[sp-2];
                            state <= STATE_SWAP_WAIT;
                        end

                        OP_OVER: begin
                            stack[sp] <= stack[sp-2];
                            sp <= sp + 1;
                            state <= STATE_FETCH;
                        end

                        OP_ROT: begin
                            // ( a b c -- b c a )
                            stack[sp-3] <= stack[sp-2];
                            stack[sp-2] <= stack[sp-1];
                            stack[sp-1] <= stack[sp-3];
                            state <= STATE_FETCH;
                        end

                        // Bitwise operations
                        OP_AND: begin
                            stack[sp-2] <= stack[sp-2] & stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_OR: begin
                            stack[sp-2] <= stack[sp-2] | stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_XOR: begin
                            stack[sp-2] <= stack[sp-2] ^ stack[sp-1];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_NOT: begin
                            stack[sp-1] <= ~stack[sp-1];
                            state <= STATE_FETCH;
                        end

                        OP_SHL: begin
                            stack[sp-2] <= stack[sp-2] << stack[sp-1][4:0];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_SHR_U: begin
                            stack[sp-2] <= stack[sp-2] >> stack[sp-1][4:0];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_SHR_S: begin
                            stack[sp-2] <= $signed(stack[sp-2]) >>> stack[sp-1][4:0];
                            sp <= sp - 1;
                            state <= STATE_FETCH;
                        end

                        OP_LOAD: begin
                            ram_addr <= stack[sp-1][9:0];
                            ram_we <= 0;
                            state <= STATE_RAM_READ;
                        end

                        OP_STORE: begin
                            ram_addr <= stack[sp-1][9:0];
                            ram_din <= stack[sp-2];
                            ram_we <= 1;
                            sp <= sp - 2;
                            state <= STATE_RAM_WAIT;
                        end

                        OP_KEY: begin
                            if (uart_rx_ready) begin
                                stack[sp] <= {24'd0, uart_rx_data};
                                sp <= sp + 1;
                                uart_rx_ack <= 1;
                                state <= STATE_KEY_WAIT;
                            end else begin
                                // Stay in STATE_EXECUTE and keep waiting
                                state <= STATE_EXECUTE;
                            end
                        end

                        OP_HALT: begin
                            state <= STATE_HALT;
                        end

                        default: begin
                            state <= STATE_FETCH;
                        end
                    endcase
                end

                STATE_UART_WAIT: begin
                    if (uart_send && uart_busy) begin
                        uart_send <= 0;
                    end
                    
                    if (!uart_busy && !uart_send) begin
                        state <= STATE_FETCH;
                    end
                end

                STATE_KEY_WAIT: begin
                    if (uart_rx_ack && !uart_rx_ready) begin
                        uart_rx_ack <= 0;
                        state <= STATE_FETCH;
                    end
                end

                STATE_RAM_READ: begin
                    // Wait one cycle for RAM to output data
                    state <= STATE_RAM_WAIT;
                end

                STATE_RAM_WAIT: begin
                    ram_we <= 0;
                    if (opcode == OP_LOAD) begin
                        stack[sp-1] <= ram_dout;
                    end
                    state <= STATE_FETCH;
                end

                STATE_SWAP_WAIT: begin
                    stack[sp-2] <= temp_swap;
                    state <= STATE_FETCH;
                end

                STATE_ALU_WAIT: begin
                    stack[sp-1] <= temp_alu;
                    state <= STATE_FETCH;
                end

                STATE_HALT: begin
                    halted <= 1;
                    status_led <= 6'b000000; // All LEDs on when halted
                end

                default: state <= STATE_FETCH;
            endcase
        end
    end

endmodule
