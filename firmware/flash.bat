@echo off
setlocal enabledelayedexpansion
echo ========================================
echo OV-Watch Build + Flash (ISP UART)
echo ========================================
echo.

:: ---- Toolchain paths (hardcoded, no env variable expansion) ----
set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "PROG_BIN=C:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin"
set "MINGW_BIN=C:\mingw64\bin"
set "PATH=%GCC_BIN%;%PROG_BIN%;%MINGW_BIN%;%PATH%"

:: ---- Verify tools exist ----
echo [0/2] Checking toolchain...
"%GCC_BIN%\arm-none-eabi-gcc.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: arm-none-eabi-gcc not found at %GCC_BIN%
    pause
    exit /b 1
)
"%MINGW_BIN%\mingw32-make.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: mingw32-make not found at %MINGW_BIN%
    pause
    exit /b 1
)
echo OK

:: ---- 1. Build ----
echo.
echo [1/2] Building firmware...
call "%MINGW_BIN%\mingw32-make.exe" -j6 -s
if errorlevel 1 (
    echo.
    echo === BUILD FAILED ===
    pause
    exit /b 1
)
echo     Build OK: build\ov-watch.hex

:: ---- 2. Flash via ISP ----
echo.
echo [2/2] Flashing via ISP (USART1 PA9/PA10)...
echo.
echo     === BOOT0 Manual Steps: ===
echo     1. BOOT0 jumper to HIGH (3.3V)
echo     2. Press RESET button
echo     3. Press any key to continue...
pause >nul

"%PROG_BIN%\STM32_Programmer_CLI.exe" -c port=COM4 br=115200 -w build\ov-watch.hex -v -s -ob
if errorlevel 1 (
    echo.
    echo === FLASH FAILED ===
    echo Check Device Manager: COM4 exists?
    pause
    exit /b 1
)

echo.
echo ========================================
echo Flash OK! Now:
echo   1. BOOT0 jumper back to LOW (GND)
echo   2. Press RESET
echo   3. Motor self-test starts!
echo ========================================
echo.
pause
