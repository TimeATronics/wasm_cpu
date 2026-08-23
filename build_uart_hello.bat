@echo off
cd /d "%~dp0verilog"
del uart_hello.json uart_hello_pnr.json uart_hello.fs 2>nul
yosys -p "read_verilog -sv uart_hello_top.sv uart_tx.sv; synth_gowin -top top -json uart_hello.json"
if errorlevel 1 exit /b 1
nextpnr-himbaechel --json uart_hello.json --write uart_hello_pnr.json --freq 27 --device GW1NR-LV9QN88PC6/I5 --vopt family=GW1N-9C --vopt cst=uart_hello.cst
if errorlevel 1 exit /b 1
gowin_pack -d GW1N-9C -o uart_hello.fs uart_hello_pnr.json
if errorlevel 1 exit /b 1
openFPGALoader -b tangnano9k uart_hello.fs -f