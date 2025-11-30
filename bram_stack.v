`default_nettype none

// Single-port BRAM-based stack for Tang Nano 9K
// Uses BSRAM efficiently by doing sequential read-then-write operations
module bram_stack #(
    parameter DEPTH_LOG2 = 5,  // 5 = 32 entries, 6 = 64 entries
    parameter WIDTH = 32
) (
    input clk,
    input reset,
    
    // Stack operations
    input push,
    input pop,
    input [WIDTH-1:0] push_data,
    output reg [WIDTH-1:0] top_data = 0,
    output reg [WIDTH-1:0] second_data = 0,
    output reg [DEPTH_LOG2-1:0] depth = 0,
    
    // Dual-access for operations like SWAP, OVER
    input read_second,  // Read sp-2
    input write_top,    // Write to sp-1
    input write_second, // Write to sp-2
    input [WIDTH-1:0] write_data
);

    localparam SIZE = 2 ** DEPTH_LOG2;
    
    // The actual BRAM
    (* ram_style = "block" *)
    reg [WIDTH-1:0] mem [0:SIZE-1];
    
    // Registered outputs from BRAM
    reg [WIDTH-1:0] mem_out = 0;
    
    // Stack pointer
    reg [DEPTH_LOG2-1:0] sp = 0;
    
    // State machine for handling multi-cycle operations
    reg [1:0] state = 0;
    localparam IDLE = 0, READ_TOP = 1, READ_SECOND = 2;
    
    always @(posedge clk) begin
        if (reset) begin
            sp <= 0;
            depth <= 0;
            state <= IDLE;
        end else begin
            case (state)
                IDLE: begin
                    if (push) begin
                        mem[sp] <= push_data;
                        sp <= sp + 1;
                        depth <= depth + 1;
                        top_data <= push_data;
                    end else if (pop) begin
                        sp <= sp - 1;
                        depth <= depth - 1;
                        // Read new top after pop
                        if (sp > 1) begin
                            top_data <= mem[sp-2];
                        end
                    end else if (write_top) begin
                        mem[sp-1] <= write_data;
                        top_data <= write_data;
                    end else if (write_second) begin
                        mem[sp-2] <= write_data;
                        second_data <= write_data;
                    end else if (read_second) begin
                        second_data <= mem[sp-2];
                    end
                    
                    // Always keep top cached
                    if (sp > 0) begin
                        top_data <= mem[sp-1];
                    end
                end
            endcase
        end
    end

endmodule

`default_nettype wire
