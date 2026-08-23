`default_nettype none

module boot_loader (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        btn,         // 0 = pressed (debug: skip load)
    // SPI flash
    output reg         flash_clk,
    input  wire        flash_miso,
    output reg         flash_mosi,
    output reg         flash_cs_n,
    // Program BRAM write port
    output reg  [13:0] prog_addr,
    output reg  [ 7:0] prog_wdata,
    output reg         prog_we,
    // Status
    output reg         boot_done
);

    localparam STATE_IDLE        = 5'd0;
    localparam STATE_READY       = 5'd1;
    localparam STATE_CMD         = 5'd2;
    localparam STATE_CMD_WAIT    = 5'd3;
    localparam STATE_ADDR        = 5'd4;
    localparam STATE_ADDR_WAIT   = 5'd5;
    localparam STATE_DATA        = 5'd6;
    localparam STATE_DATA_WAIT   = 5'd7;
    localparam STATE_DATA_STORE  = 5'd8;
    localparam STATE_NEXT_BYTE   = 5'd9;
    localparam STATE_DONE        = 5'd10;
    localparam STATE_DEBUG_LOAD  = 5'd11;

    // Debug boot ROM (synthesized as LUT-based lookup)
    function [7:0] boot_rom_get(input [3:0] addr);
        case (addr)
             0: boot_rom_get = 8'h01;
             1: boot_rom_get = 8'h3F;
             2: boot_rom_get = 8'h00;
             3: boot_rom_get = 8'h00;
             4: boot_rom_get = 8'h00;
             5: boot_rom_get = 8'h01;
             6: boot_rom_get = 8'h00;
             7: boot_rom_get = 8'hF0;
             8: boot_rom_get = 8'hFF;
             9: boot_rom_get = 8'hFF;
            10: boot_rom_get = 8'h1E;
            11: boot_rom_get = 8'h0F;
            12: boot_rom_get = 8'h0B;
            13: boot_rom_get = 8'h00;
            14: boot_rom_get = 8'h00;
            15: boot_rom_get = 8'h00;
            default: boot_rom_get = 8'h00;
        endcase
    endfunction

    reg [4:0] state = STATE_IDLE;

    reg [31:0] length;
    reg [31:0] bytes_copied;
    reg [23:0] flash_addr;

    reg [23:0] shift_reg;
    reg [3:0] bit_count;

    reg [1:0] clk_div;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= STATE_IDLE;
            flash_clk <= 0;
            flash_cs_n <= 1;
            flash_mosi <= 0;
            prog_we <= 0;
            boot_done <= 0;
            length <= 0;
            bytes_copied <= 0;
            flash_addr <= 0;
            shift_reg <= 0;
            bit_count <= 0;
            clk_div <= 0;
        end else begin
            prog_we <= 0;

            case (state)
                STATE_IDLE: begin
                    clk_div <= 0;
                    flash_cs_n <= 1;
                    if (!btn) begin
                        // Debug mode: load from internal ROM
                        bytes_copied <= 0;
                        state <= STATE_DEBUG_LOAD;
                    end else begin
                        state <= STATE_READY;
                    end
                end

                STATE_READY: begin
                    // Settle for 4 clocks
                    if (clk_div == 3) begin
                        clk_div <= 0;
                        flash_addr <= 0;
                        bytes_copied <= 0;
                        length <= 0;
                        state <= STATE_CMD;
                    end else begin
                        clk_div <= clk_div + 1;
                    end
                end

                // ── Send SPI read command ──────────────────────
                STATE_CMD: begin
                    flash_cs_n <= 0;
                    shift_reg <= {8'h03, 16'b0};  // Read command in top 8 bits
                    bit_count <= 8;
                    flash_clk <= 0;
                    state <= STATE_CMD_WAIT;
                end

                STATE_CMD_WAIT: begin
                    if (bit_count == 0) begin
                        state <= STATE_ADDR;
                        flash_addr <= 0;
                    end else begin
                        if (!flash_clk) begin
                            flash_mosi <= shift_reg[23];
                            shift_reg <= {shift_reg[22:0], 1'b0};
                            flash_clk <= 1;
                        end else begin
                            flash_clk <= 0;
                            bit_count <= bit_count - 1;
                        end
                    end
                end

                // ── Send 24-bit address ────────────────────────
                STATE_ADDR: begin
                    shift_reg <= {flash_addr[23:16], flash_addr[15:8], flash_addr[7:0]};
                    bit_count <= 24;
                    flash_clk <= 0;
                    state <= STATE_ADDR_WAIT;
                end

                STATE_ADDR_WAIT: begin
                    if (bit_count == 0) begin
                        bytes_copied <= 0;
                        bit_count <= 8;
                        state <= STATE_DATA;
                    end else begin
                        if (!flash_clk) begin
                            flash_mosi <= shift_reg[23];
                            shift_reg <= {shift_reg[22:0], 1'b0};
                            flash_clk <= 1;
                        end else begin
                            flash_clk <= 0;
                            bit_count <= bit_count - 1;
                        end
                    end
                end

                // ── Read data byte from flash ─────────────────
                STATE_DATA: begin
                    flash_clk <= 0;
                    bit_count <= 8;
                    state <= STATE_DATA_WAIT;
                end

                STATE_DATA_WAIT: begin
                    if (bit_count == 0) begin
                        state <= STATE_DATA_STORE;
                    end else begin
                        if (!flash_clk) begin
                            flash_clk <= 1;
                        end else begin
                            shift_reg <= {shift_reg[6:0], flash_miso};
                            flash_clk <= 0;
                            bit_count <= bit_count - 1;
                        end
                    end
                end

                // ── Store byte ─────────────────────────────────
                STATE_DATA_STORE: begin
                    if (bytes_copied < 4) begin
                        // Reading length header (little-endian)
                        length[bytes_copied * 8 +: 8] <= shift_reg;
                        bytes_copied <= bytes_copied + 1;
                        flash_addr <= flash_addr + 1;
                        if (bytes_copied == 3) begin
                            // Length read complete; prepare to copy program
                            bytes_copied <= 0;
                        end
                        state <= STATE_DATA;
                    end else if (bytes_copied < length) begin
                        // Writing program byte to BRAM
                        prog_addr <= bytes_copied[13:0];
                        prog_wdata <= shift_reg;
                        prog_we <= 1;
                        bytes_copied <= bytes_copied + 1;
                        flash_addr <= flash_addr + 1;
                        state <= STATE_DATA;
                    end else begin
                        // All bytes copied
                        flash_cs_n <= 1;
                        boot_done <= 1;
                        state <= STATE_DONE;
                    end
                end

                STATE_DONE: begin
                    // Stay here forever
                end

                // ── Debug load from internal ROM ────────────────
                STATE_DEBUG_LOAD: begin
                    if (bytes_copied < 16) begin
                        prog_addr <= bytes_copied[13:0];
                        prog_wdata <= boot_rom_get(bytes_copied[3:0]);
                        prog_we <= 1;
                        bytes_copied <= bytes_copied + 1;
                    end else begin
                        boot_done <= 1;
                        state <= STATE_DONE;
                    end
                end

                default: state <= STATE_IDLE;
            endcase
        end
    end

endmodule