param(
  [string]$BaseUrl = "http://localhost:8000",
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
Set-Location $root

& "$PSScriptRoot\build.ps1" -Config $Config

$exe = if ($Config -eq "Debug") {
  "$root\build\windows-debug\Debug\blackwire_client.exe"
} else {
  "$root\build\windows-release\Release\blackwire_client.exe"
}

if (-not (Test-Path $exe)) {
  throw "Client executable not found: $exe"
}

& "$PSScriptRoot\deploy-qt-runtime.ps1" -ExePath $exe -Config $Config

& $exe --smoke --base-url $BaseUrl
if ($LASTEXITCODE -ne 0) {
  throw "Smoke test failed"
}

Write-Host "Smoke test passed"
