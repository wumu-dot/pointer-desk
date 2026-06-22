@echo off
setlocal
echo ========================================
echo OV-Watch Debug Mode (ST-Link + GDB)
echo ========================================
echo.

set CUBE_CLT=C:\ST\STM32CubeCLT_1.21.0
set PATH=%CUBE_CLT%\STLink-gdb-server\bin;%CUBE_CLT%\GNU-tools-for-STM32\bin;%PATH%

echo [1] Starting ST-Link GDB Server (port 61234)...
start "ST-Link GDB Server" "%CUBE_CLT%\STLink-gdb-server\bin\ST-LINK_gdbserver.exe" -d -v -cp "C:\ST\STM32CubeCLT_1.21.0\STLink-gdb-server\bin"

echo [2] Waiting for GDB server to start...
timeout /t 3 /nobreak >nul

echo [3] Starting GDB + auto-connecting...
echo.
arm-none-eabi-gdb build\ov-watch.elf -ex "target remote :61234" -ex "monitor reset halt" -ex "load" -ex "echo === GDB ready. Type 'continue' to run. ===\n"

echo.
echo === Debug session ended ===
pause
