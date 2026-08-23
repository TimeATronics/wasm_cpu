`default_nettype none

module uart_tx
#(
    parameter DELAY_FRAMES = 234
)
(
    input  wire       clk,
    input  wire [7:0] data,
    input  wire       start,
    output reg        tx,
    output reg        busy
);
    localparam ST_IDLE      = 2'd0;
    localparam ST_START_BIT = 2'd1;
    localparam ST_WRITE     = 2'd2;
    localparam ST_STOP_BIT  = 2'd3;

    reg [1:0]  state     = ST_IDLE;
    reg [12:0] counter   = 0;
    reg [7:0]  data_out  = 0;
    reg [2:0]  bit_idx   = 0;

    always @(posedge clk) begin
        case (state)
            ST_IDLE: begin
                busy <= 0;
                tx   <= 1;
                if (start) begin
                    data_out <= data;
                    busy     <= 1;
                    state    <= ST_START_BIT;
                    counter  <= 0;
                end
            end

            ST_START_BIT: begin
                tx <= 0;
                counter <= counter + 1;
                if (counter + 1 == DELAY_FRAMES) begin
                    counter <= 0;
                    bit_idx <= 0;
                    state   <= ST_WRITE;
                end
            end

            ST_WRITE: begin
                tx <= data_out[bit_idx];
                counter <= counter + 1;
                if (counter + 1 == DELAY_FRAMES) begin
                    counter <= 0;
                    if (bit_idx == 7)
                        state <= ST_STOP_BIT;
                    else
                        bit_idx <= bit_idx + 1;
                end
            end

            ST_STOP_BIT: begin
                tx <= 1;
                counter <= counter + 1;
                if (counter + 1 == DELAY_FRAMES) begin
                    counter <= 0;
                    state   <= ST_IDLE;
                end
            end
        endcase
    end

endmodule