param(
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\.."
Set-Location $root

if (-not $SkipBuild) {
  & "$PSScriptRoot\build.ps1" -Config Release
}

$releaseDir = Join-Path $root "build/windows-release/Release"
$exe = Join-Path $releaseDir "blackwire_client.exe"
if (-not (Test-Path $exe)) {
  throw "Release executable not found: $exe"
}

& "$PSScriptRoot\deploy-qt-runtime.ps1" -ExePath $exe -Config Release

$distRoot = Join-Path $root "dist"
$bundleDir = Join-Path $distRoot "blackwire-client-windows-x64"
$zipPath = Join-Path $distRoot "blackwire-client-windows-x64.zip"

if (Test-Path $bundleDir) {
  Remove-Item -Recurse -Force $bundleDir
}
if (Test-Path $zipPath) {
  Remove-Item -Force $zipPath
}

New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
Copy-Item -Path (Join-Path $releaseDir "*") -Destination $bundleDir -Recurse -Force

Compress-Archive -Path (Join-Path $bundleDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "[package] Bundle created:"
Write-Host "  $zipPath"
Write-Host "[package] Send the extracted folder contents to the target machine and run blackwire_client.exe"
