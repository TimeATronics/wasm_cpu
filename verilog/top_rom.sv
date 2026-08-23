`default_nettype none

module top (
    input  wire       clk,
    input  wire       btn1,
    input  wire       btn2,
    output wire [5:0] led,
    output wire       uart_tx,
    input  wire       uart_rx,
    output wire       flash_clk,
    output wire       flash_cs,
    output wire       flash_mosi,
    input  wire       flash_miso
);
    wire rst_n = 1'b1;

    // ── Tiny ROM: blink test using correct MMIO addr 0xFFFFF000 ──
    // PUSH 0x3F; PUSH 0xFFFFF000; STORE; HALT
    wire [13:0] prog_addr;
    reg  [ 7:0] prog_rdata;

    always @(posedge clk)
        case (prog_addr)
            14'd0:  prog_rdata <= 8'h01; // PUSH 0x3F
            14'd1:  prog_rdata <= 8'h3F;
            14'd2:  prog_rdata <= 8'h00;
            14'd3:  prog_rdata <= 8'h00;
            14'd4:  prog_rdata <= 8'h00;
            14'd5:  prog_rdata <= 8'h01; // PUSH 0xFFFF_F000
            14'd6:  prog_rdata <= 8'h00;
            14'd7:  prog_rdata <= 8'hF0;
            14'd8:  prog_rdata <= 8'hFF;
            14'd9:  prog_rdata <= 8'hFF;
            14'd10: prog_rdata <= 8'h1E; // STORE
            14'd11: prog_rdata <= 8'hFF; // HALT
            default: prog_rdata <= 8'h00;
        endcase

    // ── Buses ──────────────────────────────────
    wire [7:0]  uart_tx_data;
    wire        uart_tx_send, uart_tx_busy;
    wire [7:0]  uart_rx_data;
    wire        uart_rx_ready, uart_rx_ack;
    wire [27:0] data_addr;
    wire [31:0] data_din, data_dout;
    wire        data_we;
    wire [3:0]  data_be;
    wire [5:0]  cpu_led;
    wire        cpu_halted;

    // ── Data BRAM ──────────────────────────────
    data_bram data_inst (
        .clk(clk), .addr(data_addr),
        .din(data_din), .dout(data_dout),
        .we(data_we), .be(data_be)
    );

    // ── CPU ────────────────────────────────────
    cpu_core cpu_inst (
        .clk(clk), .rst_n(rst_n),
        .prog_addr(prog_addr), .prog_rdata(prog_rdata),
        .prog_re(),
        .data_addr(data_addr), .data_din(data_din),
        .data_dout(data_dout), .data_we(data_we),
        .data_be(data_be),
        .uart_tx_data(uart_tx_data), .uart_tx_send(uart_tx_send),
        .uart_tx_busy(uart_tx_busy),
        .uart_rx_data(uart_rx_data), .uart_rx_ready(uart_rx_ready),
        .uart_rx_ack(uart_rx_ack),
        .led(cpu_led), .halted(cpu_halted),
        .btn({btn2, btn1}), .boot_done(1'b1)
    );

    uart_tx #(.DELAY_FRAMES(234)) tx_inst (
        .clk(clk), .data(uart_tx_data),
        .start(uart_tx_send), .tx(uart_tx), .busy(uart_tx_busy)
    );
    uart_rx #(.DELAY_FRAMES(234)) rx_inst (
        .clk(clk), .rx(uart_rx), .data(uart_rx_data),
        .data_ready(uart_rx_ready), .read_ack(uart_rx_ack)
    );

    // LEDs: active low — cpu_led is already inverted by cpu_core
    assign led = cpu_led;

    // Tie off unused flash pins
    assign flash_clk = 0;
    assign flash_cs  = 1;
    assign flash_mosi = 0;

endmodule