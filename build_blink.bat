@echo off
cd /d "%~dp0verilog"
del blink.json blink_pnr.json blink.fs 2>nul
yosys -p "read_verilog -sv top.sv cpu_core.sv data_bram.sv uart_tx.sv uart_rx.sv; synth_gowin -top top -json blink.json"
if errorlevel 1 exit /b 1
nextpnr-himbaechel --json blink.json --write blink_pnr.json --freq 27 --device GW1NR-LV9QN88PC6/I5 --vopt family=GW1N-9C --vopt cst=blink.cst
if errorlevel 1 exit /b 1
gowin_pack -d GW1N-9C -o blink.fs blink_pnr.json
if errorlevel 1 exit /b 1
openFPGALoader -b tangnano9k blink.fs -f