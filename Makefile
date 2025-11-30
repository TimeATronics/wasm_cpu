.PHONY: all synth pnr pack program clean flash assemble flash_program

PROJECT = wasm_cpu
TOP_MODULE = top
DEVICE = GW1NR-LV9QN88PC6/I5
FAMILY = GW1N-9C
BOARD = tangnano9k

SOURCES = top.v stack_cpu.v uart_tx.v flash.v
CONSTRAINTS = tangnano9k.cst

JSON = $(PROJECT).json
PNR_JSON = $(PROJECT)_pnr.json
BITSTREAM = $(PROJECT).fs

PROGRAM_BIN = programs/demo.bin

FREQ = 27

all: assemble $(BITSTREAM)

assemble: $(PROGRAM_BIN)

$(PROGRAM_BIN): programs/demo.asm scripts/assembler.js
	node scripts/assembler.js programs/demo.asm programs/demo

synth: $(JSON)

$(JSON): $(SOURCES)
	yosys -p "read_verilog -sv $(SOURCES); synth_gowin -top $(TOP_MODULE) -json $(JSON)"

pnr: $(PNR_JSON)

$(PNR_JSON): $(JSON) $(CONSTRAINTS)
	nextpnr-gowin --json $(JSON) --write $(PNR_JSON) --freq $(FREQ) --device $(DEVICE) --family $(FAMILY) --cst $(CONSTRAINTS)

pack: $(BITSTREAM)

$(BITSTREAM): $(PNR_JSON)
	gowin_pack -d $(FAMILY) -o $(BITSTREAM) $(PNR_JSON)

program: $(BITSTREAM) $(PROGRAM_BIN)
	openFPGALoader -b $(BOARD) $(BITSTREAM)

flash: $(BITSTREAM) $(PROGRAM_BIN)
	openFPGALoader -b $(BOARD) -f $(BITSTREAM)

flash_program: $(PROGRAM_BIN)
	openFPGALoader -b $(BOARD) --external-flash $(PROGRAM_BIN)

clean:
	rm -f $(JSON) $(PNR_JSON) $(BITSTREAM)
	rm -f programs/*.hex programs/*.vh programs/*.bin

monitor:
	@echo "Opening serial monitor at 115200 baud..."
	@echo "Use Ctrl+C to exit"
	screen /dev/ttyUSB1 115200
