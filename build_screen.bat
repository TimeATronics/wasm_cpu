@echo off
cd /d "%~dp0docs\lushay_tutorials\code\tangnano9k-series-examples\screen"
del screen.json screen_pnr.json screen_test.fs 2>nul
yosys -p "read_verilog screen.v; synth_gowin -top screen -json screen.json"
if errorlevel 1 exit /b 1
nextpnr-himbaechel --json screen.json --write screen_pnr.json --freq 27 --device GW1NR-LV9QN88PC6/I5 --vopt family=GW1N-9C --vopt cst=tangnano9k.cst
if errorlevel 1 exit /b 1
gowin_pack -d GW1N-9C -o screen_test.fs screen_pnr.json
if errorlevel 1 exit /b 1
openFPGALoader -b tangnano9k screen_test.fs -f