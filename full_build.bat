@echo off
echo Step 1: Assemble program
node scripts\assembler.js programs\repl_test.asm programs\repl_test

echo.
echo Step 2: Erase and program external flash
openFPGALoader -b tangnano9k --external-flash programs\repl_test.bin

echo.
echo Step 3: Build and program bitstream
call build.bat clean
call build.bat
call build.bat flash

echo.
echo Done! Press reset button on the board.
