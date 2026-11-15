param(
  [Parameter(Mandatory = $true)][string]$Executable,
  [string]$Output,
  [ValidateRange(1, 3)][int]$PixelScale = 1
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
if (-not $Output) {
  $Output = Join-Path $repoRoot ('output/ui-ux-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$outputPath = [IO.Path]::GetFullPath($Output)
if ((Test-Path -LiteralPath $outputPath) -and
    @(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) {
  throw 'Choose a new empty output folder. This test always uses disposable machines and preferences.'
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$profilePath = Join-Path $outputPath 'profile'
$documentsPath = Join-Path $outputPath 'machines'
[IO.Directory]::CreateDirectory($profilePath) | Out-Null
[IO.Directory]::CreateDirectory($documentsPath) | Out-Null
$recipe = Get-Content -LiteralPath (Join-Path $repoRoot 'native/tests/ui-ux-flow.json') -Raw |
  ConvertFrom-Json
$recipe.output = $outputPath.Replace('\', '/')
$recipe.steps[0].scale = $PixelScale
$recipe.steps[0].pixelScale = $PixelScale
$recipePath = Join-Path $outputPath 'recipe.json'
[IO.File]::WriteAllText($recipePath, ($recipe | ConvertTo-Json -Depth 30),
  [Text.UTF8Encoding]::new($false))

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $executablePath
$start.Arguments = '--qualify "' + $recipePath + '"'
$start.WorkingDirectory = Split-Path -Parent $executablePath
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['CLASSICMAC_QUALIFY'] = '1'
$start.EnvironmentVariables['CLASSICMAC_TEST_HOME'] = $profilePath
$start.EnvironmentVariables['CLASSICMAC_TEST_DOCUMENTS'] = $documentsPath
$start.EnvironmentVariables['QT_SCALE_FACTOR'] = [string]$PixelScale
$start.EnvironmentVariables['QT_ENABLE_HIGHDPI_SCALING'] = '0'
$start.EnvironmentVariables['QT_LOGGING_TO_CONSOLE'] = '1'
$start.EnvironmentVariables.Remove('QT_FONT_DPI')
$start.EnvironmentVariables.Remove('QT_SCREEN_SCALE_FACTORS')
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
if (-not $process.WaitForExit(180000)) {
  $process.Kill()
  throw "The UI qualification exceeded three minutes. Inspect $outputPath."
}
[IO.File]::WriteAllText((Join-Path $outputPath 'stdout.log'), $stdout.GetAwaiter().GetResult())
[IO.File]::WriteAllText((Join-Path $outputPath 'stderr.log'), $stderr.GetAwaiter().GetResult())
if ($process.ExitCode -ne 0) {
  $result = Join-Path $outputPath 'result.json'
  if (Test-Path -LiteralPath $result) { Get-Content -LiteralPath $result -Raw | Write-Output }
  throw "The UI qualification failed (exit $($process.ExitCode)). Inspect $outputPath."
}
$report = Get-Content -LiteralPath (Join-Path $outputPath 'result.json') -Raw | ConvertFrom-Json
if (-not $report.passed) { throw "The UI qualification reported failure. Inspect $outputPath." }
Write-Output ("Passed {0} UI steps at {1}x; screenshots and evidence: {2}" -f
  @($report.steps).Count, $PixelScale, $outputPath)
