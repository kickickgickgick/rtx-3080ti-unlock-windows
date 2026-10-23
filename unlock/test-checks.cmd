@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "GU_CHECKS_SOURCE=checks.c"
set "GU_CHECKS_TEST=build\checks-tests.exe"
set "GU_CHECKS_INCLUDE=-I tests"
set "GU_CHECKS_ARGS="
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if not exist "%CLANG%" set "CLANG=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe"
if not exist "%CLANG%" exit /b 1
if /I "%~1"=="--red" (
    set "GU_CHECKS_SOURCE=build\checks-red.c"
    set "GU_CHECKS_TEST=build\checks-red.exe"
) else if /I "%~1"=="--fixture" (
    python ..\tools\profile_tool.py header --profile ..\profiles\local-profile.json --output build\gu_profile_generated.h
    if errorlevel 1 exit /b 1
    set "GU_CHECKS_INCLUDE=-I build -I tests"
    set "GU_CHECKS_ARGS=..\evidence\rtx3080ti-run00\REPORT.TXT"
) else if not "%~1"=="" (
    echo usage: test-checks.cmd [--red^|--fixture]
    exit /b 2
)
"%CLANG%" -std=c11 -O2 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS %GU_CHECKS_INCLUDE% -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_checks.c "%GU_CHECKS_SOURCE%" -o "%GU_CHECKS_TEST%"
if errorlevel 1 exit /b 1
"%GU_CHECKS_TEST%" %GU_CHECKS_ARGS%
if errorlevel 1 exit /b 1
if /I "%~1"=="--red" exit /b 0
"%CLANG%" -target x86_64-pc-windows-msvc -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -Werror %GU_CHECKS_INCLUDE% -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 -c checks.c -o build\checks-freestanding.obj
if errorlevel 1 exit /b 1
echo PASS freestanding x86_64-pc-windows-msvc compilation
exit /b 0
