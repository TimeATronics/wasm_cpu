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
    // Reset from btn1 (pressed = reset)
    reg btn1_reg = 1;
    always @(posedge clk) btn1_reg <= btn1 ? 0 : 1;
    wire rst_n = ~btn1_reg;

    // ── Boot loader -> program BRAM ──────────────────────────
    wire [13:0] boot_addr;
    wire [7:0]  boot_wdata;
    wire        boot_we;
    wire        boot_done;

    boot_loader boot_inst (
        .clk(clk),
        .rst_n(rst_n),
        .btn(btn2),
        .flash_clk(flash_clk),
        .flash_miso(flash_miso),
        .flash_mosi(flash_mosi),
        .flash_cs_n(flash_cs),
        .prog_addr(boot_addr),
        .prog_wdata(boot_wdata),
        .prog_we(boot_we),
        .boot_done(boot_done)
    );

    // ── Program BRAM (dual port: boot write, CPU read) ──────
    wire [13:0] cpu_prog_addr;
    wire [7:0]  cpu_prog_rdata;

    prog_bram prog_inst (
        .clk(clk),
        .cpu_addr(cpu_prog_addr),
        .cpu_rdata(cpu_prog_rdata),
        .boot_addr(boot_addr),
        .boot_wdata(boot_wdata),
        .boot_we(boot_we)
    );

    // ── UART TX/RX buses ─────────────────────────────────────
    wire [7:0] uart_tx_data;
    wire       uart_tx_send, uart_tx_busy;
    wire [7:0] uart_rx_data;
    wire       uart_rx_ready, uart_rx_ack;

    // ── Data BRAM buses ──────────────────────────────────────
    wire [27:0] data_addr;
    wire [31:0] data_din, data_dout;
    wire        data_we;
    wire [3:0]  data_be;

    // ── CPU status ───────────────────────────────────────────
    wire [5:0] cpu_led;
    wire       cpu_halted;

    // ── Data BRAM ────────────────────────────────────────────
    data_bram data_inst (
        .clk(clk), .addr(data_addr),
        .din(data_din), .dout(data_dout),
        .we(data_we), .be(data_be)
    );

    // ── CPU core ─────────────────────────────────────────────
    cpu_core cpu_inst (
        .clk(clk), .rst_n(rst_n),
        .prog_addr(cpu_prog_addr), .prog_rdata(cpu_prog_rdata),
        .prog_re(),
        .data_addr(data_addr), .data_din(data_din),
        .data_dout(data_dout), .data_we(data_we),
        .data_be(data_be),
        .uart_tx_data(uart_tx_data), .uart_tx_send(uart_tx_send),
        .uart_tx_busy(uart_tx_busy),
        .uart_rx_data(uart_rx_data), .uart_rx_ready(uart_rx_ready),
        .uart_rx_ack(uart_rx_ack),
        .led(cpu_led), .halted(cpu_halted),
        .btn({btn2, btn1}), .boot_done(boot_done)
    );

    // ── UART TX/RX ───────────────────────────────────────────
    uart_tx #(.DELAY_FRAMES(234)) tx_inst (
        .clk(clk), .data(uart_tx_data),
        .start(uart_tx_send), .tx(uart_tx), .busy(uart_tx_busy)
    );
    uart_rx #(.DELAY_FRAMES(234)) rx_inst (
        .clk(clk), .rx(uart_rx), .data(uart_rx_data),
        .data_ready(uart_rx_ready), .read_ack(uart_rx_ack)
    );

    // ── LEDs: show cpu_led when running, reset pattern otherwise ──
    assign led = rst_n ? cpu_led : 6'b101010;

endmodule