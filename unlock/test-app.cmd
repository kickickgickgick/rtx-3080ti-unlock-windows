@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
python ..\tools\profile_tool.py header --profile ..\profiles\local-profile.json --output build\gu_profile_generated.h
if errorlevel 1 exit /b 1
set "APP_SRC=app.c app_io.c ownership.c format.c sha256.c checks.c"
set "APP_DEF="
if "%~1"=="red" set "APP_SRC="
if "%~1"=="red" set "APP_DEF=-DGU_EMPTY_APP"
"C:\Program Files\LLVM\bin\clang.exe" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220810deU %APP_DEF% -I build -I tests -I compat -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_app.c %APP_SRC% boot.c port.c compat.c vendor\unlock_610.c vendor\prep_610.c vendor\layout_610.c vendor\vbios_610.c vendor\gfw_eval.c -o build\app-tests.exe
if errorlevel 1 exit /b 1
build\app-tests.exe "..\evidence\rtx3080ti-run00\PROM.BIN" "..\analysis\pinned-assets-595.58.03" "..\evidence\rtx3080ti-run00\REPORT.TXT" %~2
exit /b %errorlevel%
