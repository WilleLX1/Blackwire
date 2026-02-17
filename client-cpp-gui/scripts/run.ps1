param(
  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug",
  [string]$Profile = "default",
  [switch]$Detached
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
Set-Location $root

$exe = if ($Config -eq "Debug") {
  Join-Path $root "build/windows-debug/Debug/blackwire_client.exe"
} else {
  Join-Path $root "build/windows-release/Release/blackwire_client.exe"
}

if (-not (Test-Path $exe)) {
  throw "Client executable not found: $exe. Run scripts/build.ps1 first."
}

& "$PSScriptRoot\deploy-qt-runtime.ps1" -ExePath $exe -Config $Config

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
$qtBin = if ($Config -eq "Debug") { Join-Path $qtBasePkg "debug\bin" } else { Join-Path $qtBasePkg "bin" }
$qtPlugin = if ($Config -eq "Debug") {
  Join-Path $qtBasePkg "debug\Qt6\plugins"
} else {
  Join-Path $qtBasePkg "Qt6\plugins"
}

if (Test-Path $qtBin) {
  $env:PATH = "$qtBin;$env:PATH"
}
if (Test-Path $qtPlugin) {
  $env:QT_PLUGIN_PATH = $qtPlugin
}

if ($Detached) {
  Start-Process -FilePath $exe -ArgumentList @("--profile=$Profile") | Out-Null
} else {
  & $exe "--profile=$Profile"
}
