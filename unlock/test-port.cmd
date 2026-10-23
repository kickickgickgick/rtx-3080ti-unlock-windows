@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not exist "%CLANG%" set "CLANG=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
if not exist "%CLANG%" exit /b 1
"%CLANG%" -std=c11 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I tests -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_port.c port.c -o build\test-port.exe
if errorlevel 1 exit /b 1
build\test-port.exe
if errorlevel 1 exit /b 1
call test-master-attributes.cmd
exit /b %errorlevel%
