@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not exist "%CLANG%" set "CLANG=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
if not exist "%CLANG%" exit /b 1
"%CLANG%" -std=c11 -Wall -Wextra -Werror -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 test_core.c probe_core.c probe_app.c -o build/test_core.exe
if errorlevel 1 exit /b 1
build\test_core.exe
exit /b %errorlevel%
