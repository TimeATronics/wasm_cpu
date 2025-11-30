@echo off
setlocal

set PROJECT=wasm_cpu
set TOP_MODULE=top
set DEVICE=GW1NR-LV9QN88PC6/I5
set FAMILY=GW1N-9C
set BOARD=tangnano9k

set SOURCES=top.v stack_cpu.v uart_tx.v flash.v uart_rx.v simple_ram.v
set CONSTRAINTS=tangnano9k.cst

set JSON=%PROJECT%.json
set PNR_JSON=%PROJECT%_pnr.json
set BITSTREAM=%PROJECT%.fs

set FREQ=27

if "%1"=="clean" goto clean
if "%1"=="assemble" goto assemble
if "%1"=="synth" goto synth
if "%1"=="pnr" goto pnr
if "%1"=="pack" goto pack
if "%1"=="program" goto program
if "%1"=="flash" goto flash
if "%1"=="flash_program" goto flash_program
if "%1"=="" goto all

echo Usage: build.bat [clean^|assemble^|synth^|pnr^|pack^|program^|flash^|flash_program]
goto :eof

:all
call :assemble
call :synth
call :pnr
call :pack
goto :eof

:assemble
echo Assembling repl_test.asm...
node scripts\assembler.js programs\repl_test.asm programs\repl_test
goto :eof

:clean
echo Cleaning build files...
if exist %JSON% del %JSON%
if exist %PNR_JSON% del %PNR_JSON%
if exist %BITSTREAM% del %BITSTREAM%
if exist programs\*.hex del programs\*.hex
if exist programs\*.vh del programs\*.vh
if exist programs\*.bin del programs\*.bin
goto :eof

:synth
echo Running synthesis...
yosys -p "read_verilog -sv %SOURCES%; synth_gowin -top %TOP_MODULE% -json %JSON%"
goto :eof

:pnr
echo Running place and route...
nextpnr-himbaechel --json %JSON% --write %PNR_JSON% --device %DEVICE% --vopt family=%FAMILY% --vopt cst=%CONSTRAINTS%
goto :eof

:pack
echo Packing bitstream...
gowin_pack -d %FAMILY% -o %BITSTREAM% %PNR_JSON%
goto :eof

:program
echo Programming to SRAM...
openFPGALoader -b %BOARD% %BITSTREAM%
goto :eof

:flash
echo Programming to Flash...
openFPGALoader -b %BOARD% -f %BITSTREAM%
goto :eof

:flash_program
echo Programming external flash...
openFPGALoader -b %BOARD% --external-flash programs\repl_test.bin
goto :eof
