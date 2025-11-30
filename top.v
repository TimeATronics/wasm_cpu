`default_nettype none

module top(
    input clk,
    input btn1,
    output [5:0] led,
    output uart_tx,
    input uart_rx,
    output flash_clk,
    output flash_cs,
    output flash_mosi,
    input flash_miso
);

    reg btn1Reg = 1;
    always @(negedge clk) begin
        btn1Reg <= btn1 ? 0 : 1;
    end

    wire reset = btn1Reg;

    // UART TX interface
    wire [7:0] uart_tx_data;
    wire uart_tx_send;
    wire uart_tx_busy;

    // UART RX interface
    wire [7:0] uart_rx_data;
    wire uart_rx_ready;
    wire uart_rx_ack;

    // Flash interface
    wire [23:0] flash_addr;
    wire [7:0] flash_data;
    wire flash_enable;
    wire flash_ready;

    // RAM interface
    wire ram_we;
    wire [9:0] ram_addr;
    wire [31:0] ram_din;
    wire [31:0] ram_dout;

    // CPU status
    wire [5:0] status_led;
    wire halted;

    // Instantiate Flash reader
    flash #(
        .STARTUP_WAIT(32'd10000000)
    ) flash_inst (
        .clk(clk),
        .flashClk(flash_clk),
        .flashMiso(flash_miso),
        .flashMosi(flash_mosi),
        .flashCs(flash_cs),
        .addr(flash_addr),
        .byteRead(flash_data),
        .enable(flash_enable),
        .dataReady(flash_ready)
    );

    // Instantiate UART transmitter
    uart_tx #(
        .DELAY_FRAMES(234) // 27MHz / 115200
    ) uart_tx_inst (
        .clk(clk),
        .data(uart_tx_data),
        .start(uart_tx_send),
        .tx(uart_tx),
        .busy(uart_tx_busy)
    );

    // Instantiate UART receiver
    uart_rx #(
        .DELAY_FRAMES(234) // 27MHz / 115200
    ) uart_rx_inst (
        .clk(clk),
        .rx(uart_rx),
        .data(uart_rx_data),
        .data_ready(uart_rx_ready),
        .read_ack(uart_rx_ack)
    );

    // Instantiate RAM
    simple_ram #(
        .ADDR_WIDTH(10),  // 1KB
        .DATA_WIDTH(32)
    ) ram_inst (
        .clk(clk),
        .we(ram_we),
        .addr(ram_addr),
        .din(ram_din),
        .dout(ram_dout)
    );

    // Instantiate CPU
    stack_cpu cpu_inst (
        .clk(clk),
        .reset(reset),
        .flash_addr(flash_addr),
        .flash_data(flash_data),
        .flash_enable(flash_enable),
        .flash_ready(flash_ready),
        .uart_data(uart_tx_data),
        .uart_send(uart_tx_send),
        .uart_busy(uart_tx_busy),
        .uart_rx_data(uart_rx_data),
        .uart_rx_ready(uart_rx_ready),
        .uart_rx_ack(uart_rx_ack),
        .ram_we(ram_we),
        .ram_addr(ram_addr),
        .ram_din(ram_din),
        .ram_dout(ram_dout),
        .status_led(status_led),
        .halted(halted)
    );

    // Connect status LEDs
    assign led = status_led;

endmodule
