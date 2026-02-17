param(
  [string]$VcpkgRoot = "$PSScriptRoot\..\.vcpkg"
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$msg) {
  Write-Host "[bootstrap] $msg"
}

Write-Step "Checking CMake"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  Write-Warning "CMake not found. Install CMake and rerun."
}

Write-Step "Checking MSVC toolchain"
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
  Write-Warning "MSVC cl.exe not on PATH. Run from 'Developer PowerShell for VS' or install Build Tools."
}

Write-Step "Ensuring vcpkg"
if (-not (Test-Path $VcpkgRoot)) {
  git clone https://github.com/microsoft/vcpkg $VcpkgRoot
}

& "$VcpkgRoot\bootstrap-vcpkg.bat"

$env:VCPKG_ROOT = (Resolve-Path $VcpkgRoot).Path
Write-Step "VCPKG_ROOT=$env:VCPKG_ROOT"

Push-Location "$PSScriptRoot\.."
try {
  & "$env:VCPKG_ROOT\vcpkg.exe" install --x-manifest-root . --triplet x64-windows
  Write-Step "Bootstrap completed"
} finally {
  Pop-Location
}
