param(
  [string]$EnvPath = "$PSScriptRoot/.env",
  [string]$ExamplePath = "$PSScriptRoot/example.env",
  [switch]$CreateIfMissing = $true,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function New-RandomBytes {
  param(
    [Parameter(Mandatory = $true)]
    [int]$Length
  )

  $bytes = New-Object byte[] $Length
  $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
  try {
    $rng.GetBytes($bytes)
  } finally {
    $rng.Dispose()
  }
  return $bytes
}

function New-UrlSafeSecret {
  param(
    [Parameter(Mandatory = $true)]
    [int]$ByteLength
  )

  $bytes = New-RandomBytes -Length $ByteLength
  $text = [Convert]::ToBase64String($bytes)
  return $text.TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

function New-Base64Secret {
  param(
    [Parameter(Mandatory = $true)]
    [int]$ByteLength
  )

  $bytes = New-RandomBytes -Length $ByteLength
  return [Convert]::ToBase64String($bytes)
}

function Set-OrAppendEnvValue {
  param(
    [Parameter(Mandatory = $true)]
    [System.Collections.Generic.List[string]]$Lines,
    [Parameter(Mandatory = $true)]
    [string]$Key,
    [Parameter(Mandatory = $true)]
    [string]$Value
  )

  $pattern = "^(?<prefix>\s*$([Regex]::Escape($Key))\s*=\s*).*$"
  for ($i = 0; $i -lt $Lines.Count; $i++) {
    if ($Lines[$i] -match $pattern) {
      $prefix = $Matches["prefix"]
      $Lines[$i] = "$prefix$Value"
      return
    }
  }

  [void]$Lines.Add("$Key=$Value")
}

if (-not (Test-Path -Path $EnvPath)) {
  if (-not $CreateIfMissing) {
    throw "Env file not found: $EnvPath"
  }
  if (-not (Test-Path -Path $ExamplePath)) {
    throw "Cannot create env file. Example file not found: $ExamplePath"
  }
  Copy-Item -Path $ExamplePath -Destination $EnvPath
}

$lines = [System.Collections.Generic.List[string]]::new()
foreach ($line in [System.IO.File]::ReadAllLines($EnvPath)) {
  [void]$lines.Add($line)
}

$jwtSecret = New-UrlSafeSecret -ByteLength 48
$federationSeed = New-Base64Secret -ByteLength 32

Set-OrAppendEnvValue -Lines $lines -Key "BLACKWIRE_JWT_SECRET_KEY" -Value $jwtSecret
Set-OrAppendEnvValue -Lines $lines -Key "BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64" -Value $federationSeed

if ($DryRun) {
  Write-Output "Dry run: no file changes written."
  Write-Output "Would rotate:"
  Write-Output "  BLACKWIRE_JWT_SECRET_KEY"
  Write-Output "  BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64"
  exit 0
}

[System.IO.File]::WriteAllLines($EnvPath, $lines)

Write-Output "Updated $EnvPath"
Write-Output "Rotated:"
Write-Output "  BLACKWIRE_JWT_SECRET_KEY"
Write-Output "  BLACKWIRE_FEDERATION_SIGNING_PRIVATE_KEY_B64"
