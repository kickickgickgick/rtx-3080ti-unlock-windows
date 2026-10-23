param([switch]$WithLocalFixtures)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$logDir=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$jobs=@(
 @{Name='probe';Kind='cmd';Path='test.cmd'},
 @{Name='port';Kind='cmd';Path='unlock/test-port.cmd'},
 @{Name='checks';Kind='cmd';Path='unlock/test-checks.cmd'},
 @{Name='runtime';Kind='cmd';Path='unlock/test-runtime.cmd'},
 @{Name='pe';Kind='python';Path='unlock/tests/test_pe.py'},
 @{Name='usb-package';Kind='python';Path='tools/test_package_usb.py'},
 @{Name='profile-tool';Kind='python';Path='tools/test_profile_tool.py'}
)
if($WithLocalFixtures) {
 $jobs+=@(
  @{Name='3080ti-profile';Kind='cmd';Path='unlock/test-3080ti-profile.cmd'},
  @{Name='captured-checks';Kind='cmd';Path='unlock/test-checks.cmd';Args=@('--fixture')},
  @{Name='transaction';Kind='cmd';Path='unlock/test-transaction.cmd'},
  @{Name='sha';Kind='cmd';Path='unlock/test-sha.cmd'},
  @{Name='efi-transaction';Kind='cmd';Path='unlock/test-efi-transaction.cmd'},
  @{Name='app-current';Kind='cmd';Path='unlock/test-app-current.cmd'},
  @{Name='fwsec-sequence';Kind='cmd';Path='unlock/test-fwsec-sequence.cmd'}
 )
}
$results=@()
foreach($job in $jobs) {
 $log=Join-Path $logDir ($job.Name+'.log')
 $code=1
 try {
  $ErrorActionPreference='Continue'
  $path=Join-Path $project $job.Path
  $jobArgs=@($job.Args)
  if($job.Kind -eq 'cmd') { & $path @jobArgs *> $log }
  else { & python $path *> $log }
  $code=$LASTEXITCODE
 } catch { $_ | Out-String | Add-Content -LiteralPath $log }
 finally { $ErrorActionPreference='Stop' }
 $results += [ordered]@{Name=$job.Name;Command=$job.Path;ExitCode=$code;Log=($job.Name+'.log')}
 Write-Output ($job.Name+': exit '+$code)
 Get-Content -Tail 1 -LiteralPath $log
}
$passed=@($results | Where-Object {$_.ExitCode -ne 0}).Count -eq 0
[ordered]@{AllPassed=$passed;HardwareAccess=$false;WithLocalFixtures=[bool]$WithLocalFixtures;Results=$results} |
 ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $logDir 'test-results.json')
if(-not $passed){throw 'One or more verification groups failed.'}
Write-Output ('All '+$jobs.Count+' selected verification groups passed; hardware was simulated.')
