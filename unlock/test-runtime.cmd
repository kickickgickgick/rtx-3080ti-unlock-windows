@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CC=C:\Program Files\LLVM\bin\clang.exe"
if not exist "%CC%" set "CC=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
if not exist "%CC%" exit /b 1
set "FLAGS=-std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -fno-builtin"
if "%~1"=="red" (
  "%CC%" %FLAGS% -DGU_EMPTY_RUNTIME tests\test_runtime.c -o build\runtime-red.exe
  if errorlevel 1 exit /b 1
  build\runtime-red.exe
  exit /b 1
)
"%CC%" %FLAGS% -Dmemcpy=GuTestMemcpy -Dmemset=GuTestMemset -Dmemchr=GuTestMemchr -Dmemcmp=GuTestMemcmp -Dstrcmp=GuTestStrcmp -Dstrlen=GuTestStrlen -c runtime.c -o build\runtime-host.obj
if errorlevel 1 exit /b 1
"%CC%" %FLAGS% tests\test_runtime.c format.c build\runtime-host.obj -o build\runtime-tests.exe
if errorlevel 1 exit /b 1
build\runtime-tests.exe
exit /b %errorlevel%
