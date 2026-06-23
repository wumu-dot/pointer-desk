@echo off
setlocal enabledelayedexpansion
echo ========================================
echo OV-Watch Debug Mode (ST-Link GDB Server)
echo ========================================
echo.

set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "STLINK_BIN=C:\ST\STM32CubeCLT_1.21.0\STLink-gdb-server\bin"
set "PATH=%GCC_BIN%;%STLINK_BIN%;%PATH%"

echo [1] Starting ST-Link GDB Server (port 7184)...
start "ST-Link GDB Server" "%STLINK_BIN%\ST-LINK_gdbserver.exe" -d -v

echo [2] Waiting for GDB server to start...
timeout /t 3 /nobreak >nul

echo [3] Starting GDB + auto-connecting...
echo.
"%GCC_BIN%\arm-none-eabi-gdb.exe" build\ov-watch.elf -ex "target remote :7184" -ex "monitor reset halt" -ex "load" -ex "echo === GDB ready. Type 'continue' to run. ===\n"

echo.
echo === Debug session ended ===
pause
