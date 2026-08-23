@echo off
cd /d "%~dp0verilog"
del top.json top_pnr.json top_test.fs 2>nul
yosys -p "read_verilog -sv top.sv cpu_core.sv prog_bram.sv data_bram.sv boot_loader.sv uart_tx.sv uart_rx.sv; synth_gowin -top top -json top.json"
if errorlevel 1 exit /b 1
nextpnr-himbaechel --json top.json --write top_pnr.json --freq 27 --device GW1NR-LV9QN88PC6/I5 --vopt family=GW1N-9C --vopt cst=../tangnano9k.cst
if errorlevel 1 exit /b 1
gowin_pack -d GW1N-9C -o top_test.fs top_pnr.json
if errorlevel 1 exit /b 1
echo BUILD SUCCESS