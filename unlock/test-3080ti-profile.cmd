@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not exist "%CLANG%" set "CLANG=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
if not exist "%CLANG%" exit /b 1
"%CLANG%" -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I vendor tests\test_3080ti_profile.c vendor\vbios_610.c vendor\prep_610.c vendor\layout_610.c -o build\test-3080ti-profile.exe
if errorlevel 1 exit /b 1
build\test-3080ti-profile.exe "..\evidence\rtx3080ti-run00\PROM.BIN"
exit /b %errorlevel%
