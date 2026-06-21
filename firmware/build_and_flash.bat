@echo off
setlocal enabledelayedexpansion

echo ========================================
echo OV-Watch / pointer-desk — Build & Flash
echo ========================================
echo.

:: ---- Path: adjust if your CubeCLT lives elsewhere ----
set GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin
set PROG_BIN=C:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin
set MINGW_BIN=C:\mingw64\bin
set PATH=%GCC_BIN%;%PROG_BIN%;%MINGW_BIN%;%PATH%

:: ---- 0. Verify toolchain ----
echo [0/3] Checking toolchain...
where arm-none-eabi-gcc >nul 2>&1
if errorlevel 1 (
    echo ❌ arm-none-eabi-gcc not found. Is STM32CubeCLT installed?
    pause
    exit /b 1
)
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo ❌ STM32_Programmer_CLI not found. Is CubeProgrammer installed?
    pause
    exit /b 1
)
echo ✅ Toolchain OK

:: ---- 1. Build ----
echo.
echo [1/3] Building firmware...
mingw32-make -j6
if errorlevel 1 (
    echo.
    echo ❌ Build failed!
    pause
    exit /b %errorlevel%
)
echo ✅ Build OK

:: ---- 2. Flash ----
echo.
echo [2/3] Flashing firmware...
STM32_Programmer_CLI -c port=SWD freq=4000 -w build\ov-watch.hex -v -rst
if errorlevel 1 (
    echo.
    echo ❌ Flash failed! Check ST-Link connection and board power.
    pause
    exit /b %errorlevel%
)
echo ✅ Flash OK

echo.
echo [3/3] Done! Board is running.
echo.
pause
