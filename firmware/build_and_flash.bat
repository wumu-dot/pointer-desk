@echo off
setlocal enabledelayedexpansion
echo ========================================
echo OV-Watch Build + Flash (ST-Link SWD)
echo ========================================
echo.

:: ---- Toolchain paths ----
set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "PROG_BIN=C:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin"
set "MINGW_BIN=C:\mingw64\bin"
set "PATH=%GCC_BIN%;%PROG_BIN%;%MINGW_BIN%;%PATH%"

:: ---- 0. Verify toolchain ----
echo [0/3] Checking toolchain...
"%GCC_BIN%\arm-none-eabi-gcc.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: arm-none-eabi-gcc not found
    pause
    exit /b 1
)
"%PROG_BIN%\STM32_Programmer_CLI.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: STM32_Programmer_CLI not found
    pause
    exit /b 1
)
echo OK

:: ---- 1. Build ----
echo.
echo [1/3] Building firmware...
call "%MINGW_BIN%\mingw32-make.exe" -j6
if errorlevel 1 (
    echo.
    echo === BUILD FAILED ===
    pause
    exit /b 1
)
echo OK

:: ---- 2. Flash via ST-Link SWD ----
echo.
echo [2/3] Flashing via ST-Link SWD...
"%PROG_BIN%\STM32_Programmer_CLI.exe" -c port=SWD freq=4000 -w build\ov-watch.hex -v -rst
if errorlevel 1 (
    echo.
    echo === FLASH FAILED ===
    echo Check ST-Link connection and board power.
    pause
    exit /b 1
)
echo OK

:: ---- 3. Done ----
echo.
echo [3/3] Done! Board is running.
echo Motor self-test starts in ~1s...
echo.
pause
