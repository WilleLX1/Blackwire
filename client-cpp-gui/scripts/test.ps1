param(
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug",
  [switch]$NoSubst
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$mappedDrive = $null
$createdMappedDrive = $false
$originalLocation = (Get-Location).Path

function Get-SubstDrive {
  $used = Get-PSDrive -PSProvider FileSystem | ForEach-Object { "$($_.Name):" }
  foreach ($candidate in @("Z:", "Y:", "X:", "W:", "V:", "U:", "T:")) {
    if ($used -contains $candidate) {
      continue
    }
    return $candidate
  }
  return $null
}

function Normalize-PathString {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Value
  )

  return $Value.Trim().TrimEnd('\').ToLowerInvariant()
}

function Get-SubstMappings {
  $map = @{}
  $lines = & subst 2>$null
  foreach ($line in $lines) {
    if ($line -match '^([A-Z]):\\: => (.+)$') {
      $drive = "$($matches[1].ToUpper()):"
      $target = $matches[2].Trim()
      $map[$drive] = $target
    }
  }
  return $map
}

function Invoke-Checked {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Description,
    [Parameter(Mandatory = $true)]
    [scriptblock]$Command
  )

  & $Command
  if ($LASTEXITCODE -ne 0) {
    throw "$Description failed with exit code $LASTEXITCODE."
  }
}

try {
  $substMappings = Get-SubstMappings
  $rootDrive = $root.Substring(0, 2).ToUpper()
  if ($substMappings.ContainsKey($rootDrive)) {
    Write-Host "[test] Running from existing subst drive $rootDrive"
    $NoSubst = $true
  }

  if (-not $NoSubst) {
    $normalizedRoot = Normalize-PathString -Value $root
    $existingMapping = $null
    foreach ($entry in $substMappings.GetEnumerator()) {
      if ((Normalize-PathString -Value $entry.Value) -eq $normalizedRoot) {
        $existingMapping = $entry
        break
      }
    }

    if ($existingMapping) {
      $mappedDrive = $existingMapping.Key
      $root = "$mappedDrive\"
      Write-Host "[test] Reusing existing subst drive $mappedDrive"
    } else {
      $drive = Get-SubstDrive
      if ($drive) {
        & subst $drive $root | Out-Null
        if ($LASTEXITCODE -eq 0) {
          $mappedDrive = $drive
          $createdMappedDrive = $true
          $root = "$drive\"
          Write-Host "[test] Using subst drive $mappedDrive for shorter paths"
        }
      }
    }
  }

  Set-Location $root

  $localVcpkgPath = Join-Path $root ".vcpkg"
  if (Test-Path $localVcpkgPath) {
    $env:VCPKG_ROOT = $localVcpkgPath
  }

  $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
  if (-not (Test-Path $toolchain)) {
    throw "vcpkg toolchain not found at $toolchain. Run scripts/bootstrap-windows.ps1 first."
  }

  $preset = if ($Config -eq "Debug") { "windows-debug" } else { "windows-release" }
  $buildPreset = if ($Config -eq "Debug") { "build-debug" } else { "build-release" }
  $testPreset = "test-debug"

  Invoke-Checked "CMake configure" { cmake --preset $preset }
  Invoke-Checked "CMake build" { cmake --build --preset $buildPreset }
  Invoke-Checked "CTest" { ctest --preset $testPreset }
}
finally {
  Set-Location $originalLocation
  if ($createdMappedDrive -and $mappedDrive) {
    & subst $mappedDrive /D | Out-Null
    Write-Host "[test] Removed subst drive $mappedDrive"
  }
}
