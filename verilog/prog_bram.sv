`default_nettype none

module prog_bram (
    input  wire        clk,
    // CPU read port (combinational)
    input  wire [13:0] cpu_addr,
    output reg  [ 7:0] cpu_rdata,
    // Boot loader write port (synchronous)
    input  wire [13:0] boot_addr,
    input  wire [ 7:0] boot_wdata,
    input  wire        boot_we
);

    reg [7:0] mem [0:16383];

    initial begin
`include "led_test_init.vh"
    end

    always @(posedge clk) begin
        if (boot_we)
            mem[boot_addr] <= boot_wdata;
        cpu_rdata <= mem[cpu_addr];
    end

endmodule