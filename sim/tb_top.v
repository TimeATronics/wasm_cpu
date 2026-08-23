`default_nettype none
`timescale 1ns/1ps

module tb_top;
    reg clk = 0;
    reg btn1 = 1;  // 1=not pressed (reset inactive), 0=pressed (reset)
    reg btn2 = 0;  // 0=pressed (S2 = debug skip)
    wire [5:0] led;
    wire uart_tx;
    reg uart_rx = 1;
    wire flash_clk, flash_cs, flash_mosi;
    reg flash_miso = 0;

    top uut (
        .clk(clk),
        .btn1(btn1),
        .btn2(btn2),
        .led(led),
        .uart_tx(uart_tx),
        .uart_rx(uart_rx),
        .flash_clk(flash_clk),
        .flash_cs(flash_cs),
        .flash_mosi(flash_mosi),
        .flash_miso(flash_miso)
    );

    always #18.519 clk = ~clk;  // 27 MHz

    initial begin
        $dumpfile("sim/tb_top.vcd");
        $dumpvars(0, tb_top);

        // btn1_reg initial=1 => rst_n=0 for ~1 cycle, then btn1=1 => rst_n=1
        btn1 = 1; btn2 = 0;
        #200;

        $display("Time=%0t: Starting sim (S2 pressed). rst_n=%b boot_done=%b",
                 $time, uut.rst_n, uut.boot_done);
        $display("Time=%0t: boot_loader state=%0d", $time, uut.boot_inst.state);

        #10000;
        $display("Time=%0t: rst_n=%b boot_done=%b led=%b pc=%0d state=%0d",
                 $time, uut.rst_n, uut.boot_done, led, uut.cpu_inst.pc, uut.cpu_inst.state);

        #100000;
        $display("Time=%0t: rst_n=%b boot_done=%b led=%b pc=%0d state=%0d",
                 $time, uut.rst_n, uut.boot_done, led, uut.cpu_inst.pc, uut.cpu_inst.state);

        #1000000;
        $display("Time=%0t: led=%b pc=%0d state=%0d tos=%0d dsp=%0d halted=%0d",
                 $time, led, uut.cpu_inst.pc, uut.cpu_inst.state,
                 uut.cpu_inst.tos, uut.cpu_inst.dsp, uut.cpu_inst.halted);

        $finish;
    end
endmodule