param(
 [Parameter(Mandatory=$true)][string]$Package,
 [Parameter(Mandatory=$true)][string]$Machine,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][ValidateSet(100,125,150,175,200)][int]$DesktopScale,
 [ValidateRange(55000,110000)][int]$BootWaitMilliseconds=55000
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
 throw 'Run in an active Windows desktop session'
}
$Package=(Resolve-Path $Package).Path
$Machine=(Resolve-Path $Machine).Path
if (-not (Test-Path (Join-Path $Machine 'config.json'))) { throw 'Missing disposable Mac config' }
if (Test-Path $Output) { throw 'Choose a new output folder' }
New-Item -ItemType Directory $Output | Out-Null
$Output=(Resolve-Path $Output).Path
$recipe=Get-Content (Join-Path $PSScriptRoot 'desktop-flow.json') -Raw | ConvertFrom-Json
$recipe.output=$Output.Replace('\','/')
$recipe.steps[0].scale=$DesktopScale/100.0
$recipe.steps[0].pixelScale=[Math]::Max(1,[Math]::Round($DesktopScale/100.0,[MidpointRounding]::AwayFromZero))
$recipe.steps[1].folder=$Machine.Replace('\','/')
foreach ($step in $recipe.steps) {
 if ($step.action -eq 'wait' -and $step.milliseconds -eq 55000) { $step.milliseconds=$BootWaitMilliseconds }
}
$recipePath=Join-Path $Output 'recipe.json'
[IO.File]::WriteAllText($recipePath,($recipe|ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
$env:CLASSICMAC_TEST_HOME=Join-Path $Output 'home'
$env:CLASSICMAC_TEST_DOCUMENTS=Join-Path $Output 'documents'
$env:CLASSICMAC_QUALIFY='1'
Remove-Item Env:CLASSICMAC_RUNTIME,Env:QT_SCALE_FACTOR,Env:QT_SCREEN_SCALE_FACTORS -ErrorAction SilentlyContinue
$actor=Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'native-input.ps1')+'"'),'-Output',('"'+$Output+'"') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Output 'native-input.log') -RedirectStandardError (Join-Path $Output 'native-input-errors.log') -PassThru
$p=Start-Process $Package -ArgumentList '--qualify',('"'+$recipePath+'"') -Wait -PassThru
$p.ExitCode | Set-Content (Join-Path $Output 'exit.txt')
$actor.WaitForExit(5000) | Out-Null
$inputCompleted=Test-Path (Join-Path $Output 'native-input-complete.json')
$inputErrors=(Get-Item (Join-Path $Output 'native-input-errors.log')).Length
$config=Get-Content (Join-Path $Machine 'config.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$expected='Windows QA '+[char]0xE9+' '+[char]0xF1
$flow=Get-Content (Join-Path $Output 'result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$report=@{passed=($p.ExitCode -eq 0 -and $flow.passed -and $actor.HasExited -and $inputCompleted -and $inputErrors -eq 0 -and $config.name -eq $expected);packageExit=$p.ExitCode;inputCompleted=$inputCompleted;inputErrors=$inputErrors;name=$config.name;scale=$DesktopScale}
[IO.File]::WriteAllText((Join-Path $Output 'input-result.json'),($report|ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
if (-not $report.passed) { throw 'Windows desktop flow failed; inspect result and input logs' }
