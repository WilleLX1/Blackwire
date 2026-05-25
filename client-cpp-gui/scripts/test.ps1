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

function Normalize-CMakePathString {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Value
  )

  return $Value.Trim().Replace('\', '/').TrimEnd('/').ToLowerInvariant()
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

function Resolve-VisualStudioCMakeTool {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ToolName
  )

  $fromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
  if ($fromPath) {
    return $fromPath.Source
  }

  $candidates = @(
    "$env:ProgramFiles\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$ToolName",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$ToolName",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$ToolName",
    "$env:ProgramFiles\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$ToolName",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$ToolName"
  )

  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  throw "$ToolName was not found in PATH or the usual Visual Studio install locations."
}

function Clear-StaleCMakeConfigureState {
  param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,
    [Parameter(Mandatory = $true)]
    [string]$Toolchain
  )

  $normalizedBuildDir = Normalize-CMakePathString -Value $BuildDir
  $normalizedBuildRoot = Normalize-CMakePathString -Value $BuildRoot
  if (-not $normalizedBuildDir.StartsWith("$normalizedBuildRoot/")) {
    throw "Refusing to clear CMake state outside build root: $BuildDir"
  }

  $cachePath = Join-Path $BuildDir "CMakeCache.txt"
  $cmakeFilesPath = Join-Path $BuildDir "CMakeFiles"
  if (-not (Test-Path $cachePath) -and -not (Test-Path $cmakeFilesPath)) {
    return
  }

  $expectedBuildDir = Normalize-CMakePathString -Value $BuildDir
  $expectedToolchain = Normalize-CMakePathString -Value $Toolchain
  $staleReasons = @()

  if (Test-Path $cachePath) {
    $cacheText = Get-Content -Raw -LiteralPath $cachePath
    if ($cacheText -match '(?m)^CMAKE_CACHEFILE_DIR:INTERNAL=(.+)$') {
      $cacheBuildDir = Normalize-CMakePathString -Value $matches[1]
      if ($cacheBuildDir -ne $expectedBuildDir) {
        $staleReasons += "cache build dir is $($matches[1])"
      }
    }
    if ($cacheText -match '(?m)^CMAKE_TOOLCHAIN_FILE:[^=]+=(.+)$') {
      $cacheToolchain = Normalize-CMakePathString -Value $matches[1]
      if ($cacheToolchain -ne $expectedToolchain) {
        $staleReasons += "cache toolchain is $($matches[1])"
      }
    }
    if ($cacheText -match '(?m)^Z_VCPKG_POWERSHELL_PATH:[^=]+=(.+)$') {
      $powerShellPath = $matches[1].Trim()
      if (-not $powerShellPath.EndsWith(".exe", [StringComparison]::OrdinalIgnoreCase)) {
        $staleReasons += "cache PowerShell path is $powerShellPath"
      }
    }
  }

  if (Test-Path $cmakeFilesPath) {
    $systemFiles = Get-ChildItem -LiteralPath $cmakeFilesPath -Filter CMakeSystem.cmake -Recurse -ErrorAction SilentlyContinue
    foreach ($systemFile in $systemFiles) {
      $systemText = Get-Content -Raw -LiteralPath $systemFile.FullName
      if ($systemText -match 'include\("([^"]*vcpkg\.cmake)"\)') {
        $systemToolchain = Normalize-CMakePathString -Value $matches[1]
        if ($systemToolchain -ne $expectedToolchain) {
          $staleReasons += "system toolchain is $($matches[1])"
          break
        }
      }
    }
  }

  if (-not $staleReasons) {
    return
  }

  Write-Host "[test] Clearing stale CMake configure state: $($staleReasons -join '; ')"
  if (Test-Path $cachePath) {
    Remove-Item -LiteralPath $cachePath -Force
  }
  if (Test-Path $cmakeFilesPath) {
    Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force
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
  $cmakeCommand = Resolve-VisualStudioCMakeTool -ToolName "cmake.exe"
  $ctestCommand = Resolve-VisualStudioCMakeTool -ToolName "ctest.exe"
  Clear-StaleCMakeConfigureState `
    -BuildDir (Join-Path $root "build/$preset") `
    -BuildRoot (Join-Path $root "build") `
    -Toolchain $toolchain

  Invoke-Checked "CMake configure" { & $cmakeCommand --preset $preset }
  Invoke-Checked "CMake build" { & $cmakeCommand --build --preset $buildPreset }
  Invoke-Checked "CTest" { & $ctestCommand --preset $testPreset }
}
finally {
  Set-Location $originalLocation
  if ($createdMappedDrive -and $mappedDrive) {
    & subst $mappedDrive /D | Out-Null
    Write-Host "[test] Removed subst drive $mappedDrive"
  }
}
