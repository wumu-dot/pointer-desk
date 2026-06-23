@echo off
setlocal enabledelayedexpansion
echo ========================================
echo OV-Watch Debug Mode (OpenOCD + GDB)
echo ========================================
echo.

set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "OCD_BIN=C:\OpenOCD\xpack-openocd-0.12.0-7\bin"
set "PATH=%GCC_BIN%;%OCD_BIN%;%PATH%"

echo [1] Starting OpenOCD (port 3333)...
start "OpenOCD [port 3333]" "%OCD_BIN%\openocd.exe" -f openocd.cfg

echo [2] Waiting for OpenOCD to start...
timeout /t 3 /nobreak >nul

echo [3] Starting GDB + auto-connecting...
echo.
"%GCC_BIN%\arm-none-eabi-gdb.exe" build\ov-watch.elf

echo.
echo === Debug session ended ===
pause
