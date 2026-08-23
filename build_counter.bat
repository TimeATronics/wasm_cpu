@echo off
cd /d "%~dp0docs\lushay_tutorials\code\tangnano9k-series-examples\counter"
del counter.json counter_pnr.json counter_test.fs 2>nul
yosys -p "read_verilog counter.v; synth_gowin -top counter -json counter.json"
if errorlevel 1 exit /b 1
nextpnr-himbaechel --json counter.json --write counter_pnr.json --freq 27 --device GW1NR-LV9QN88PC6/I5 --vopt family=GW1N-9C --vopt cst=tangnano9k.cst
if errorlevel 1 exit /b 1
gowin_pack -d GW1N-9C -o counter_test.fs counter_pnr.json
if errorlevel 1 exit /b 1
openFPGALoader -b tangnano9k counter_test.fs -f