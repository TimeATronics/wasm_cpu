`default_nettype none

module uart_rx
#(
    parameter DELAY_FRAMES = 234
)
(
    input  wire       clk,
    input  wire       rx,
    output reg [7:0]  data,
    output reg        data_ready,
    input  wire       read_ack
);
    localparam HALF_DELAY = DELAY_FRAMES / 2;

    localparam ST_IDLE      = 3'd0;
    localparam ST_START_BIT = 3'd1;
    localparam ST_READ_WAIT = 3'd2;
    localparam ST_READ      = 3'd3;
    localparam ST_STOP_BIT  = 3'd4;

    reg [2:0]  state     = ST_IDLE;
    reg [12:0] counter   = 0;
    reg [7:0]  shift_reg = 0;
    reg [2:0]  bit_idx   = 0;

    always @(posedge clk) begin
        case (state)
            ST_IDLE: begin
                if (read_ack) data_ready <= 0;
                if (rx == 0) begin
                    state    <= ST_START_BIT;
                    counter  <= 1;
                    bit_idx  <= 0;
                end
            end

            ST_START_BIT: begin
                counter <= counter + 1;
                if (counter == HALF_DELAY) begin
                    state   <= ST_READ;
                    counter <= 1;
                end
            end

            ST_READ: begin
                counter <= counter + 1;
                if (counter == HALF_DELAY) begin
                    shift_reg <= {rx, shift_reg[7:1]};
                    counter   <= 0;
                    if (bit_idx == 7)
                        state <= ST_STOP_BIT;
                    else begin
                        bit_idx <= bit_idx + 1;
                        state   <= ST_READ_WAIT;
                    end
                end
            end

            ST_READ_WAIT: begin
                counter <= counter + 1;
                if (counter + 1 == DELAY_FRAMES) begin
                    counter <= 0;
                    state   <= ST_READ;
                end
            end

            ST_STOP_BIT: begin
                counter <= counter + 1;
                if (counter + 1 == DELAY_FRAMES) begin
                    data       <= shift_reg;
                    data_ready <= 1;
                    state      <= ST_IDLE;
                    counter    <= 0;
                end
            end
        endcase
    end

endmodule