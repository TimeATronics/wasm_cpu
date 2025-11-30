`default_nettype none

module uart_rx
#(
    parameter DELAY_FRAMES = 234 // 27MHz / 115200 baud = ~234
)
(
    input clk,
    input rx,
    output reg [7:0] data = 0,
    output reg data_ready = 0,
    input read_ack
);

    localparam STATE_IDLE = 0;
    localparam STATE_START = 1;
    localparam STATE_DATA = 2;
    localparam STATE_STOP = 3;

    reg [2:0] state = STATE_IDLE;
    reg [7:0] counter = 0;
    reg [2:0] bitIndex = 0;
    reg [7:0] shiftReg = 0;

    always @(posedge clk) begin
        case (state)
            STATE_IDLE: begin
                counter <= 0;
                bitIndex <= 0;
                data_ready <= 0;
                if (rx == 0) begin // Start bit detected
                    state <= STATE_START;
                end
            end

            STATE_START: begin
                if (counter == (DELAY_FRAMES / 2) - 1) begin
                    // Sample in the middle of start bit
                    counter <= 0;
                    state <= STATE_DATA;
                end else begin
                    counter <= counter + 1;
                end
            end

            STATE_DATA: begin
                if (counter == DELAY_FRAMES - 1) begin
                    counter <= 0;
                    shiftReg <= {rx, shiftReg[7:1]};
                    if (bitIndex == 7) begin
                        state <= STATE_STOP;
                    end else begin
                        bitIndex <= bitIndex + 1;
                    end
                end else begin
                    counter <= counter + 1;
                end
            end

            STATE_STOP: begin
                if (counter == DELAY_FRAMES - 1) begin
                    data <= shiftReg;
                    data_ready <= 1;
                    state <= STATE_IDLE;
                end else begin
                    counter <= counter + 1;
                end
            end

            default: state <= STATE_IDLE;
        endcase

        // Clear data_ready when acknowledged
        if (read_ack && data_ready) begin
            data_ready <= 0;
        end
    end

endmodule
