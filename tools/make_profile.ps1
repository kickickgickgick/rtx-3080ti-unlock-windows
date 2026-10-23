param(
    [Parameter(Mandatory=$true)][string]$ProbeConfig,
    [Parameter(Mandatory=$true)][string]$Prom,
    [string]$Output
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Output) { $Output = Join-Path $root 'profiles\local-profile.json' }
$probePath = (Resolve-Path -LiteralPath $ProbeConfig).Path
$promPath = (Resolve-Path -LiteralPath $Prom).Path
$outputPath = [System.IO.Path]::GetFullPath($Output)
$llvmCandidates = @(
    'C:/Program Files/LLVM/bin',
    'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/Llvm/x64/bin',
    'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin'
)
$clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($clangCommand) { $llvmCandidates = @((Split-Path -Parent $clangCommand.Source)) + $llvmCandidates }
$llvmRoot = $llvmCandidates | Where-Object { Test-Path -LiteralPath "$_/clang.exe" } | Select-Object -First 1
if (-not $llvmRoot) { throw 'LLVM clang.exe was not found; PROM FWSEC validation cannot run.' }
$validationDir = Join-Path $env:TEMP ('rtx3080ti-profile-validation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $validationDir -Force | Out-Null
$validator = Join-Path $validationDir 'validate-prom.exe'
try {
    & "$llvmRoot/clang.exe" -std=c11 -O1 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS `
        -I "$root/unlock/vendor" "$root/unlock/tests/test_3080ti_profile.c" `
        "$root/unlock/vendor/vbios_610.c" "$root/unlock/vendor/prep_610.c" `
        "$root/unlock/vendor/layout_610.c" -o $validator
    if ($LASTEXITCODE -ne 0) { throw 'PROM validator compilation failed.' }
    & $validator $promPath
    if ($LASTEXITCODE -ne 0) { throw 'PROM FWSEC validation failed; no profile was created.' }
    & python "$PSScriptRoot/profile_tool.py" create --probe $probePath --prom $promPath --output $outputPath
    if ($LASTEXITCODE -ne 0) { throw 'Profile generation failed.' }
    Write-Output "Created local machine profile: $outputPath"
} finally {
    if (Test-Path -LiteralPath $validationDir) { Remove-Item -LiteralPath $validationDir -Recurse -Force }
}
