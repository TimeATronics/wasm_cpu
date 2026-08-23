.PHONY: all synth pnr pack program clean flash flash_program

PROJECT = wasm_cpu
TOP_MODULE = top
DEVICE = GW1NR-LV9QN88PC6/I5
FAMILY = GW1N-9C
BOARD = tangnano9k

SOURCES = verilog/top.sv verilog/cpu_core.sv verilog/prog_bram.sv verilog/data_bram.sv verilog/boot_loader.sv uart_tx.v uart_rx.v
CONSTRAINTS = tangnano9k.cst

JSON = $(PROJECT).json
PNR_JSON = $(PROJECT)_pnr.json
BITSTREAM = $(PROJECT).fs

FREQ = 27

all: synth pnr pack

synth: $(JSON)

$(JSON): $(SOURCES)
	yosys -p "read_verilog -sv $(SOURCES); synth_gowin -top $(TOP_MODULE) -json $(JSON)"

pnr: $(PNR_JSON)

$(PNR_JSON): $(JSON) $(CONSTRAINTS)
	nextpnr-himbaechel --json $(JSON) --write $(PNR_JSON) --device $(DEVICE) --vopt family=$(FAMILY) --vopt cst=$(CONSTRAINTS)

pack: $(BITSTREAM)

$(BITSTREAM): $(PNR_JSON)
	gowin_pack -d $(FAMILY) -o $(BITSTREAM) $(PNR_JSON)

program: $(BITSTREAM)
	openFPGALoader -b $(BOARD) $(BITSTREAM)

flash: $(BITSTREAM)
	openFPGALoader -b $(BOARD) -f $(BITSTREAM)

clean:
	rm -f $(JSON) $(PNR_JSON) $(BITSTREAM)
	rm -f programs/*.hex programs/*.vh programs/*.bin