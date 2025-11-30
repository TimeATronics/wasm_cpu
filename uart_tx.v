`default_nettype none

module uart_tx
#(
    parameter DELAY_FRAMES = 234 // 27MHz / 115200 baud = ~234
)
(
    input clk,
    input [7:0] data,
    input start,
    output reg tx = 1,
    output reg busy = 0
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
                tx <= 1;
                busy <= 0;
                counter <= 0;
                bitIndex <= 0;
                if (start) begin
                    shiftReg <= data;
                    busy <= 1;
                    state <= STATE_START;
                end
            end

            STATE_START: begin
                tx <= 0; // Start bit
                if (counter == DELAY_FRAMES - 1) begin
                    counter <= 0;
                    state <= STATE_DATA;
                end else begin
                    counter <= counter + 1;
                end
            end

            STATE_DATA: begin
                tx <= shiftReg[0];
                if (counter == DELAY_FRAMES - 1) begin
                    counter <= 0;
                    shiftReg <= {1'b0, shiftReg[7:1]};
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
                tx <= 1; // Stop bit
                if (counter == DELAY_FRAMES - 1) begin
                    state <= STATE_IDLE;
                end else begin
                    counter <= counter + 1;
                end
            end

            default: state <= STATE_IDLE;
        endcase
    end

endmodule
