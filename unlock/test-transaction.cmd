@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
"%CLANG%" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -D_CRT_SECURE_NO_WARNINGS -D__KERNEL__ -DGU_TARGET_PCI_ID=0x220810deu -I tests/transaction_shim tests/test_transaction_3080.c vendor/prep_610.c vendor/layout_610.c vendor/vbios_610.c vendor/gfw_eval.c -o build/transaction-3080ti.exe
if errorlevel 1 exit /b 1
build\transaction-3080ti.exe "..\evidence\rtx3080ti-run00\PROM.BIN" "..\analysis\pinned-assets-595.58.03"
exit /b %errorlevel%
