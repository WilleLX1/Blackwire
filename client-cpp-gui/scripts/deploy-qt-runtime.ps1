param(
  [Parameter(Mandatory = $true)]
  [string]$ExePath,
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

function Copy-FileBestEffort {
  param(
    [Parameter(Mandatory = $true)]
    [string]$SourcePath,
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir,
    [string]$Label = "file"
  )

  if (-not (Test-Path $SourcePath)) {
    Write-Warning "Missing $Label source: $SourcePath"
    return $false
  }

  $destPath = Join-Path $DestinationDir (Split-Path $SourcePath -Leaf)
  try {
    Copy-Item $SourcePath -Destination $DestinationDir -Force
    return $true
  } catch {
    $isLocked = $_.Exception -is [System.IO.IOException] -and
      $_.Exception.Message -match "used by another process"
    if ($isLocked -and (Test-Path $destPath)) {
      Write-Host "[deploy] Skipping locked $Label already present: $(Split-Path $destPath -Leaf)"
      return $true
    }
    throw
  }
}

function Get-VsInstallPath {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) {
    return $null
  }

  try {
    $path = & $vswhere -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($path)) {
      return $null
    }
    return $path.Trim()
  } catch {
    return $null
  }
}

function Get-MsvcRuntimeSearchDirs {
  param(
    [ValidateSet("Debug", "Release")]
    [string]$Config
  )

  $dirs = [System.Collections.Generic.List[string]]::new()

  if ($env:VCToolsRedistDir) {
    $vcRedistRoot = $env:VCToolsRedistDir.TrimEnd("\")
    if (Test-Path $vcRedistRoot) {
      if ($Config -eq "Debug") {
        $debugDirs = Get-ChildItem -Path $vcRedistRoot -Directory -Recurse -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -like "*debug_nonredist*x64*" }
        foreach ($entry in $debugDirs) {
          $dirs.Add($entry.FullName)
        }
      } else {
        $crtDirs = Get-ChildItem -Path $vcRedistRoot -Directory -Recurse -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -like "Microsoft.VC*.CRT" -and $_.FullName -like "*x64*" }
        foreach ($entry in $crtDirs) {
          $dirs.Add($entry.FullName)
        }
      }
    }
  }

  $vsInstallPath = Get-VsInstallPath
  if ($vsInstallPath) {
    if ($Config -eq "Debug") {
      $debugRoot = Join-Path $vsInstallPath "VC\Redist\MSVC"
      if (Test-Path $debugRoot) {
        $debugDirs = Get-ChildItem -Path $debugRoot -Directory -ErrorAction SilentlyContinue |
          Sort-Object Name -Descending |
          ForEach-Object {
            Join-Path $_.FullName "debug_nonredist\x64\Microsoft.VC*.DebugCRT"
          }
        foreach ($pattern in $debugDirs) {
          $matches = Get-ChildItem -Path $pattern -Directory -ErrorAction SilentlyContinue
          foreach ($entry in $matches) {
            $dirs.Add($entry.FullName)
          }
        }
      }
    } else {
      $redistRoot = Join-Path $vsInstallPath "VC\Redist\MSVC"
      if (Test-Path $redistRoot) {
        $crtDirs = Get-ChildItem -Path $redistRoot -Directory -ErrorAction SilentlyContinue |
          Sort-Object Name -Descending |
          ForEach-Object {
            Join-Path $_.FullName "x64\Microsoft.VC*.CRT"
          }
        foreach ($pattern in $crtDirs) {
          $matches = Get-ChildItem -Path $pattern -Directory -ErrorAction SilentlyContinue
          foreach ($entry in $matches) {
            $dirs.Add($entry.FullName)
          }
        }
      }
    }
  }

  $system32 = Join-Path $env:windir "System32"
  if (Test-Path $system32) {
    $dirs.Add($system32)
  }

  return $dirs | Select-Object -Unique
}

function Copy-MsvcRuntimeDlls {
  param(
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir,
    [ValidateSet("Debug", "Release")]
    [string]$Config
  )

  $runtimePatterns = if ($Config -eq "Debug") {
    @("msvcp140*d.dll", "vcruntime140*d.dll", "concrt140d.dll")
  } else {
    @("msvcp140*.dll", "vcruntime140*.dll", "concrt140.dll")
  }

  $searchDirs = Get-MsvcRuntimeSearchDirs -Config $Config
  if (-not $searchDirs -or $searchDirs.Count -eq 0) {
    Write-Warning "No MSVC runtime search paths discovered."
    return
  }

  $copied = 0
  $copiedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
  foreach ($pattern in $runtimePatterns) {
    $matchedThisPattern = $false
    foreach ($dir in $searchDirs) {
      $candidates = Get-ChildItem -Path (Join-Path $dir $pattern) -File -ErrorAction SilentlyContinue
      if (-not $candidates) {
        continue
      }

      foreach ($candidate in $candidates) {
        if ($copiedNames.Add($candidate.Name)) {
          if (Copy-FileBestEffort -SourcePath $candidate.FullName -DestinationDir $DestinationDir -Label "MSVC runtime DLL") {
            $copied += 1
          }
        }
      }
      $matchedThisPattern = $true
      break
    }

    if (-not $matchedThisPattern) {
      Write-Warning "Could not locate runtime DLLs matching: $pattern"
    }
  }

  if ($copied -eq 0) {
    Write-Warning "No MSVC runtime DLLs were copied; target machine may need VC++ redistributable."
  } else {
    Write-Host "[deploy] Copied $copied MSVC runtime DLL(s)"
  }
}

$root = Resolve-Path "$PSScriptRoot\.."
Set-Location $root

if (-not (Test-Path $ExePath)) {
  throw "Executable not found: $ExePath"
}

function Resolve-PackageDir {
  param(
    [Parameter(Mandatory = $true)]
    [string]$BaseName,
    [ValidateSet("Debug", "Release")]
    [string]$Config
  )

  $candidates = @(
    (Join-Path $root ".vcpkg\packages\${BaseName}_x64-windows"),
    (Join-Path $root ".vcpkg\packages\${BaseName}_x64-windows-release")
  )

  foreach ($candidate in $candidates) {
    if ($Config -eq "Debug") {
      $marker = Join-Path $candidate "debug\Qt6\plugins\platforms\qwindowsd.dll"
      if (Test-Path $marker) {
        return $candidate
      }
      continue
    }

    $marker = Join-Path $candidate "Qt6\plugins\platforms\qwindows.dll"
    if (Test-Path $marker) {
      return $candidate
    }
  }

  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  return $candidates[0]
}

$qtBasePkg = Resolve-PackageDir -BaseName "qtbase" -Config $Config
$qtWsPkg = Resolve-PackageDir -BaseName "qtwebsockets" -Config $Config
Write-Host "[deploy] Qt base package: $qtBasePkg"
Write-Host "[deploy] Qt websockets package: $qtWsPkg"

$qtBin = if ($Config -eq "Debug") { Join-Path $qtBasePkg "debug\bin" } else { Join-Path $qtBasePkg "bin" }
$qtWsBin = if ($Config -eq "Debug") { Join-Path $qtWsPkg "debug\bin" } else { Join-Path $qtWsPkg "bin" }
$platformDll = if ($Config -eq "Debug") {
  Join-Path $qtBasePkg "debug\Qt6\plugins\platforms\qwindowsd.dll"
} else {
  Join-Path $qtBasePkg "Qt6\plugins\platforms\qwindows.dll"
}

$exeDir = Split-Path -Parent $ExePath
$platformDir = Join-Path $exeDir "platforms"
New-Item -ItemType Directory -Force -Path $platformDir | Out-Null

$windeployCandidates = @(
  (Join-Path $qtBasePkg "tools\Qt6\bin\windeployqt.exe"),
  (Join-Path $qtBasePkg "tools\Qt6\bin\windeployqt6.exe")
)

$windeploy = $windeployCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$qtRuntimeReady = $false
if ($windeploy) {
  Write-Host "[deploy] Using $windeploy"
  $mode = if ($Config -eq "Debug") { "--debug" } else { "--release" }
  & $windeploy $mode --no-translations --force --dir $exeDir $ExePath
  if ($LASTEXITCODE -eq 0) {
    Write-Host "[deploy] windeployqt completed"
    $qtRuntimeReady = $true
  } else {
    Write-Warning "windeployqt failed, applying fallback copy deployment"
  }
}

if (-not $qtRuntimeReady) {
  if (-not (Test-Path $platformDll)) {
    throw "Qt platform plugin not found: $platformDll"
  }
  [void](Copy-FileBestEffort -SourcePath $platformDll -DestinationDir $platformDir -Label "Qt platform plugin")

  $qtDlls = if ($Config -eq "Debug") {
    @("Qt6Cored.dll", "Qt6Guid.dll", "Qt6Widgetsd.dll", "Qt6Networkd.dll", "Qt6WebSocketsd.dll")
  } else {
    @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6WebSockets.dll")
  }

  foreach ($dll in $qtDlls) {
    $src = Join-Path $qtBin $dll
    if (-not (Test-Path $src)) {
      $src = Join-Path $qtWsBin $dll
    }
    if (Test-Path $src) {
      [void](Copy-FileBestEffort -SourcePath $src -DestinationDir $exeDir -Label "Qt runtime DLL")
    } else {
      Write-Warning "Missing expected Qt runtime DLL: $dll"
    }
  }

  Write-Host "[deploy] Fallback deployment completed"
}

Copy-MsvcRuntimeDlls -DestinationDir $exeDir -Config $Config
