$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

$llvmCandidates = @(
    'C:/Program Files/LLVM/bin',
    'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/Llvm/x64/bin',
    'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin'
)
$clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($clangCommand) {
    $llvmCandidates = @((Split-Path -Parent $clangCommand.Source)) + $llvmCandidates
}
$llvmRoot = $llvmCandidates | Where-Object {
    (Test-Path -LiteralPath "$_/clang.exe") -and
    (Test-Path -LiteralPath "$_/lld-link.exe")
} | Select-Object -First 1
if (-not $llvmRoot) {
    throw 'LLVM clang.exe and lld-link.exe were not found in the supported Visual Studio locations.'
}

$output = 'build/probe'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @('probe_app', 'probe_core', 'efi_runtime')
$objects = @()
foreach ($source in $sources) {
    $object = "$output/$source.obj"
    & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 `
        -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone `
        -fshort-wchar -Os -Wall -Wextra -Werror `
        -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 `
        -c "$source.c" -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}

& "$llvmRoot/lld-link.exe" /nologo /nodefaultlib `
    /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 `
    /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf `
    "/map:$output/PROBE.map" "/out:$output/BOOTX64.EFI" @objects
if ($LASTEXITCODE -ne 0) { throw 'EFI probe link failed' }

& python unlock/verify_pe.py "$output/BOOTX64.EFI" "$output/PROBE.map" `
    --output "$output/pe-verification.json"
if ($LASTEXITCODE -ne 0) { throw 'EFI probe PE verification failed' }

[ordered]@{
    BuiltUtc = [DateTime]::UtcNow.ToString('o')
    Purpose = 'Read-only RTX 3080 Ti profile probe; performs no GPU writes or resets'
    SupportedPci = '10de:2208'
    TargetSelection = 'runtime candidate selection; subsystem and PCI topology are discovered'
    EfiSha256 = (Get-FileHash -LiteralPath "$output/BOOTX64.EFI" -Algorithm SHA256).Hash
    PeVerificationSha256 = (Get-FileHash -LiteralPath "$output/pe-verification.json" -Algorithm SHA256).Hash
    HardwareTested = $false
} | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath "$output/build-manifest.json"

$usbRoot = "$output/usb-root"
New-Item -ItemType Directory -Path "$usbRoot/EFI/BOOT" -Force | Out-Null
New-Item -ItemType Directory -Path "$usbRoot/RTX3080TI-PROBE" -Force | Out-Null
Copy-Item -LiteralPath "$output/BOOTX64.EFI" `
    -Destination "$usbRoot/EFI/BOOT/BOOTX64.EFI" -Force

Write-Output "Built read-only probe: $output/BOOTX64.EFI"
Write-Output "Prepared USB contents: $usbRoot"
