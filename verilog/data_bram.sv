`default_nettype none

module data_bram (
    input  wire        clk,
    input  wire [27:0] addr,
    input  wire [31:0] din,
    output reg  [31:0] dout,
    input  wire        we,
    input  wire [ 3:0] be
);

    reg [31:0] mem [0:2047];

    always @(posedge clk) begin
        if (we) begin
            if (be[0]) mem[addr[10:0]][ 7: 0] <= din[ 7: 0];
            if (be[1]) mem[addr[10:0]][15: 8] <= din[15: 8];
            if (be[2]) mem[addr[10:0]][23:16] <= din[23:16];
            if (be[3]) mem[addr[10:0]][31:24] <= din[31:24];
        end
        dout <= mem[addr[10:0]];
    end

endmodule