`default_nettype none

module top (
    input  wire clk,
    output wire uart_tx,
    output wire [5:0] led
);
    wire tx_busy;
    wire tx_line;

    // ── UART TX ──────────────────────────────
    reg [7:0]  tx_data;
    reg        tx_start;

    uart_tx #(.DELAY_FRAMES(234)) tx_inst (
        .clk  (clk),
        .data (tx_data),
        .start(tx_start),
        .tx   (tx_line),
        .busy (tx_busy)
    );
    assign uart_tx = tx_line;

    // ── LED alive blink ──────────────────────
    reg [24:0] led_ctr = 0;
    always @(posedge clk) led_ctr <= led_ctr + 1;
    assign led = {5'b11111, led_ctr[24]};

    // ── Message ROM (18 bytes) ───────────────
    reg [7:0] byte_ctr = 0;
    reg [24:0] gap_ctr = 0;

    always @(posedge clk) begin
        tx_start <= 0;

        if (!tx_busy) begin
            if (gap_ctr > 0) begin
                gap_ctr <= gap_ctr - 1;
            end else begin
                tx_start <= 1;
                case (byte_ctr)
                    0:  tx_data <= "H";
                    1:  tx_data <= "e";
                    2:  tx_data <= "l";
                    3:  tx_data <= "l";
                    4:  tx_data <= "o";
                    5:  tx_data <= ",";
                    6:  tx_data <= " ";
                    7:  tx_data <= "W";
                    8:  tx_data <= "S";
                    9:  tx_data <= "O";
                    10: tx_data <= "K";
                    11: tx_data <= "!";
                    12: tx_data <= "\r";
                    13: tx_data <= "\n";
                    default: tx_data <= 0;
                endcase

                if (byte_ctr == 13) begin
                    byte_ctr <= 0;
                    gap_ctr  <= 27_000_000;  // ~1 second pause
                end else begin
                    byte_ctr <= byte_ctr + 1;
                    gap_ctr  <= 50000;  // short gap between chars
                end
            end
        end
    end

endmodule