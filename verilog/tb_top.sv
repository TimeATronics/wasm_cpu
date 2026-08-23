`default_nettype none
`timescale 1ns / 1ps

module tb_top;
    reg clk = 0;
    reg btn1 = 0;
    reg btn2 = 0;
    reg uart_rx = 1;
    wire [5:0] led;
    wire uart_tx;
    wire flash_clk, flash_cs, flash_mosi;
    reg  flash_miso = 0;

    // 27 MHz clock
    always #18.518 clk = !clk;

    top dut (
        .clk(clk), .btn1(btn1), .btn2(btn2),
        .led(led), .uart_tx(uart_tx), .uart_rx(uart_rx),
        .flash_clk(flash_clk), .flash_cs(flash_cs),
        .flash_mosi(flash_mosi), .flash_miso(flash_miso)
    );

    // Simple flash model: always returns 0xFF (erased flash)
    // Write a test program into the flash model
    reg [7:0] flash_mem [0:1023];
    reg [23:0] flash_addr;
    reg [7:0] flash_bit_count;
    reg flash_cmd;
    reg [7:0] flash_byte;

    initial begin
        // Pre-load a tiny test program: just HALT
        // Format: 4-byte length, then code
        flash_mem[0] = 8'h01;  // length = 1 byte
        flash_mem[1] = 8'h00;
        flash_mem[2] = 8'h00;
        flash_mem[3] = 8'h00;
        flash_mem[4] = 8'hFF;  // HALT opcode
    end

    initial begin
        $dumpfile("tb_top.vcd");
        $dumpvars(0, tb_top);
        btn1 = 0;
        #100 btn1 = 1;  // release reset
        #5000000 $display("Simulation timeout — boot_done check");
        $finish;
    end

endmodule