param([string]$Profile)
$ErrorActionPreference = 'Stop'
$invocationRoot = (Get-Location).Path
Set-Location -LiteralPath $PSScriptRoot
if (-not $Profile) { $Profile = Join-Path $PSScriptRoot '..\profiles\local-profile.json' }
elseif (-not [System.IO.Path]::IsPathRooted($Profile)) { $Profile = Join-Path $invocationRoot $Profile }
$profilePath = (Resolve-Path -LiteralPath $Profile -ErrorAction Stop).Path
New-Item -ItemType Directory -Path 'build' -Force | Out-Null
& python ../tools/profile_tool.py header --profile $profilePath --output build/gu_profile_generated.h
if ($LASTEXITCODE -ne 0) { throw 'Machine profile validation/header generation failed.' }
$profileData = Get-Content -Raw -LiteralPath $profilePath | ConvertFrom-Json
$llvmCandidates = @(
    'C:/Program Files/LLVM/bin',
    'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/Llvm/x64/bin',
    'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin'
)
$clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
if ($clangCommand) { $llvmCandidates = @((Split-Path -Parent $clangCommand.Source)) + $llvmCandidates }
$llvmRoot = $llvmCandidates | Where-Object {
    (Test-Path -LiteralPath "$_/clang.exe") -and (Test-Path -LiteralPath "$_/lld-link.exe")
} | Select-Object -First 1
if (-not $llvmRoot) { throw 'LLVM clang.exe and lld-link.exe were not found.' }
New-Item -ItemType Directory -Path 'build' -Force | Out-Null
$sources = @('app','app_io','boot','ownership','format','runtime','sha256','checks','port','compat',
    'vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval')
$objects = @()
foreach ($source in $sources) {
    $object = 'build/' + ($source -replace '/','_') + '.obj'
    & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x220810deU -I build -I compat -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
& "$llvmRoot/lld-link.exe" /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /map:build/BOOTX64.map /out:build/BOOTX64.EFI @objects
if ($LASTEXITCODE -ne 0) { throw 'EFI link failed' }
& python verify_pe.py build/BOOTX64.EFI build/BOOTX64.map --output build/pe-verification.json
if ($LASTEXITCODE -ne 0) { throw 'PE verification failed' }
$inputs = @(Get-ChildItem -LiteralPath . -Recurse -File | Where-Object {
    $_.Extension -in '.c','.h','.cmd','.ps1','.py','.psm1','.json' -and $_.FullName -notlike '*\build\*' -and $_.FullName -notlike '*\releases\*'
} | Sort-Object FullName | ForEach-Object {
    [ordered]@{ Path=$_.FullName.Substring($PSScriptRoot.Length+1); Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
[ordered]@{
    BuiltUtc=[DateTime]::UtcNow.ToString('o');
    TargetPci=('{0:x8}' -f [uint32]$profileData.pci.identity);
    TargetSubsystem=('{0:x8}' -f [uint32]$profileData.pci.subsystem);
    TargetBdf=('{0:x4}:{1:x2}:{2:x2}.{3:x1}' -f [uint32]$profileData.pci.segment,[uint32]$profileData.pci.bus,[uint32]$profileData.pci.device,[uint32]$profileData.pci.function);
    MachineProfileSha256=(Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash;
    SourceManifestSha256=(Get-FileHash -LiteralPath '../SOURCE-MANIFEST.json' -Algorithm SHA256).Hash;
    EfiSha256=(Get-FileHash -LiteralPath 'build/BOOTX64.EFI' -Algorithm SHA256).Hash;
    PeVerificationSha256=(Get-FileHash -LiteralPath 'build/pe-verification.json' -Algorithm SHA256).Hash;
    Edk2Commit='c1415771ac592ad8509351c46cd3cda4f38d7d52'; Sources=$inputs;
    HardwareTested=$false
} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath 'build/build-manifest.json'
Write-Output 'Built and inspected locally. Run test-all.ps1, then tools/package_usb.py to prepare a local USB directory.'
