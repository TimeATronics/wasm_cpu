`default_nettype none

module simple_ram
#(
    parameter ADDR_WIDTH = 10,  // 1KB RAM (2^10 = 1024 bytes)
    parameter DATA_WIDTH = 32   // 32-bit words
)
(
    input clk,
    input we,
    input [ADDR_WIDTH-1:0] addr,
    input [DATA_WIDTH-1:0] din,
    output reg [DATA_WIDTH-1:0] dout = 0
);

    reg [DATA_WIDTH-1:0] ram [0:(1 << ADDR_WIDTH) - 1];

    always @(posedge clk) begin
        if (we) begin
            ram[addr] <= din;
        end
        dout <= ram[addr];
    end

endmodule
