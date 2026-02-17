param(
  [switch]$DryRun,
  [string]$Profile
)

$ErrorActionPreference = "Stop"

function Get-NormalizedProfile {
  param(
    [string]$RawValue
  )

  if ([string]::IsNullOrWhiteSpace($RawValue)) {
    return ""
  }

  $value = $RawValue.Trim().ToLowerInvariant()
  $chars = foreach ($ch in $value.ToCharArray()) {
    if ([char]::IsLetterOrDigit($ch) -or $ch -eq '-' -or $ch -eq '_') {
      $ch
    } else {
      '_'
    }
  }

  $normalized = -join $chars
  while ($normalized.Contains("__")) {
    $normalized = $normalized.Replace("__", "_")
  }

  if ([string]::IsNullOrWhiteSpace($normalized)) {
    return "default"
  }

  return $normalized
}

function Remove-PathIfExists {
  param(
    [Parameter(Mandatory = $true)]
    [string]$PathValue
  )

  if (-not (Test-Path $PathValue)) {
    Write-Host "[reset] Not found: $PathValue"
    return
  }

  if ($DryRun) {
    Write-Host "[dry-run] Would remove: $PathValue"
    return
  }

  Remove-Item -Path $PathValue -Recurse -Force
  Write-Host "[reset] Removed: $PathValue"
}

function Get-MatchedCredentialTargets {
  param(
    [string[]]$Targets,
    [string]$NormalizedProfile
  )

  if ([string]::IsNullOrWhiteSpace($NormalizedProfile)) {
    return $Targets | Where-Object { $_ -match "(?i)blackwire" }
  }

  if ($NormalizedProfile -eq "default") {
    return $Targets | Where-Object { $_ -match "(?i)blackwire:" -and $_ -notmatch "(?i)blackwire:profile:" }
  }

  $needle = [Regex]::Escape("blackwire:profile:${NormalizedProfile}:")
  return $Targets | Where-Object { $_ -match "(?i)$needle" }
}

function Remove-BlackwireCredentials {
  param(
    [string]$NormalizedProfile
  )

  $cmdkeyOutput = & cmdkey /list 2>$null
  if ($LASTEXITCODE -ne 0) {
    Write-Warning "Unable to list Credential Manager entries via cmdkey."
    return
  }

  $targets = @()
  foreach ($line in $cmdkeyOutput) {
    if ($line -match "^\s*Target:\s*(.+)$") {
      $targets += $matches[1].Trim()
    }
  }

  $blackwireTargets = Get-MatchedCredentialTargets -Targets $targets -NormalizedProfile $NormalizedProfile |
    Sort-Object -Unique

  if (-not $blackwireTargets -or $blackwireTargets.Count -eq 0) {
    if ([string]::IsNullOrWhiteSpace($NormalizedProfile)) {
      Write-Host "[reset] No Credential Manager entries matched 'blackwire'."
    } else {
      Write-Host "[reset] No Credential Manager entries matched profile '$NormalizedProfile'."
    }
    return
  }

  foreach ($target in $blackwireTargets) {
    if ($DryRun) {
      Write-Host "[dry-run] Would delete credential: $target"
      continue
    }

    & cmdkey "/delete:$target" | Out-Null
    if ($LASTEXITCODE -eq 0) {
      Write-Host "[reset] Deleted credential: $target"
    } else {
      Write-Warning "Failed to delete credential: $target"
    }
  }
}

$normalizedProfile = Get-NormalizedProfile -RawValue $Profile
$appDataRoot = Join-Path $env:APPDATA "blackwire_client"

$pathsToRemove = @()
if ([string]::IsNullOrWhiteSpace($normalizedProfile)) {
  $pathsToRemove += $appDataRoot
} elseif ($normalizedProfile -eq "default") {
  $pathsToRemove += (Join-Path $appDataRoot "client_state.json")
} else {
  $pathsToRemove += (Join-Path $appDataRoot (Join-Path "profiles" $normalizedProfile))
}

$pathsToRemove = $pathsToRemove | Sort-Object -Unique
foreach ($pathValue in $pathsToRemove) {
  Remove-PathIfExists -PathValue $pathValue
}

Remove-BlackwireCredentials -NormalizedProfile $normalizedProfile

if ($DryRun) {
  Write-Host "[dry-run] Reset preview completed."
} else {
  Write-Host "[reset] Completed."
}
