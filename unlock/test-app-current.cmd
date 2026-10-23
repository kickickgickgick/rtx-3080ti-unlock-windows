@echo off
setlocal
cd /d "%~dp0"
call test-app.cmd normal handoff-success
if errorlevel 1 exit /b 1
for %%S in (current-start-returns) do (
  build\app-tests.exe "..\evidence\rtx3080ti-run00\PROM.BIN" "..\analysis\pinned-assets-595.58.03" "..\evidence\rtx3080ti-run00\REPORT.TXT" %%S
  if errorlevel 1 exit /b 1
)
echo Current 3080 Ti PCI-attribute-preserving headless scenarios passed.
exit /b 0
