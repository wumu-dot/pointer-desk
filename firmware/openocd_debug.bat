@echo off
setlocal
echo ========================================
echo OV-Watch Debug Mode (OpenOCD + GDB)
echo ========================================
echo.

set CUBE_CLT=C:\ST\STM32CubeCLT_1.21.0
set PATH=%CUBE_CLT%\GNU-tools-for-STM32\bin;%PATH%
set PATH=C:\OpenOCD\xpack-openocd-0.12.0-7\bin;%PATH%

echo [1] Starting OpenOCD (port 3333)...
start "OpenOCD [port 3333]" openocd.exe -f openocd.cfg

echo [2] Waiting for OpenOCD to start...
timeout /t 3 /nobreak >nul

echo [3] Starting GDB + auto-connecting...
echo.
arm-none-eabi-gdb build\ov-watch.elf

echo.
echo === Debug session ended ===
pause
