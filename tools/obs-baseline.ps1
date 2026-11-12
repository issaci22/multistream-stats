<#
.SYNOPSIS
  Read-only proof that the real OBS installation is not modified by this project.

  -Mode Capture : records size + last-write time + SHA-256 of every file under
                  the OBS root into logs\obs-baseline.json
  -Mode Verify  : re-scans and reports added / removed / changed files.

  Files under config\obs-studio\logs, crashes, profiler_data and the browser
  cache change whenever the user runs OBS themselves; they are reported in a
  separate "user activity" group instead of being hidden.
#>
param(
    [ValidateSet('Capture', 'Verify')]
    [string]$Mode = 'Capture',
    [string]$ObsRoot = "$env:USERPROFILE\Documents\OBS Studio",
    # Baseline file name (without .json) under logs\; lets Phase 7 keep the
    # original development baseline while taking a fresh pre-install snapshot.
    [string]$Name = 'obs-baseline'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$baselinePath = Join-Path $logs "$Name.json"

function Scan([string]$dir) {
    $result = @{}
    Get-ChildItem -LiteralPath $dir -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($dir.Length).TrimStart('\')
        $hash = try { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 -ErrorAction Stop).Hash } catch { 'UNREADABLE' }
        $result[$rel] = [ordered]@{ size = $_.Length; mtime = $_.LastWriteTimeUtc.ToString('o'); sha256 = $hash }
    }
    return $result
}

$activity = '^config\\obs-studio\\(logs|crashes|profiler_data|\.sentinel|plugin_config\\obs-browser\\)'

if ($Mode -eq 'Capture') {
    $scan = Scan $ObsRoot
    [ordered]@{ captured = (Get-Date).ToString('o'); root = $ObsRoot; files = $scan } |
        ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $baselinePath
    "Captured $($scan.Count) files from $ObsRoot -> $baselinePath"
    exit 0
}

if (-not (Test-Path $baselinePath)) { throw 'No baseline captured yet.' }
$base = (Get-Content $baselinePath -Raw | ConvertFrom-Json)
$old = @{}
$base.files.PSObject.Properties | ForEach-Object { $old[$_.Name] = $_.Value }
$now = Scan $ObsRoot

$changes = New-Object System.Collections.Generic.List[string]
foreach ($k in $now.Keys) {
    if (-not $old.ContainsKey($k)) { $changes.Add("ADDED    $k") }
    elseif ($old[$k].sha256 -ne $now[$k].sha256) { $changes.Add("CHANGED  $k") }
}
foreach ($k in $old.Keys) { if (-not $now.ContainsKey($k)) { $changes.Add("REMOVED  $k") } }

$userActivity = @($changes | Where-Object { $_.Substring(9) -match $activity })
$install = @($changes | Where-Object { $_.Substring(9) -notmatch $activity })

$report = @("OBS baseline verification $(Get-Date -Format s)", "baseline: $($base.captured)", "files now: $($now.Count), in baseline: $($old.Count)")
$report += "Installation / settings changes: $($install.Count)"
$report += $install | ForEach-Object { "  $_" }
$report += "User-activity files (logs, crash dumps, profiler, browser cache): $($userActivity.Count)"
$report += $userActivity | ForEach-Object { "  $_" }
$report += "RESULT: " + $(if ($install.Count -eq 0) { 'PASS - OBS installation and settings unchanged' } else { 'CHANGES FOUND - review list above' })
$report | Set-Content -Encoding utf8 (Join-Path $logs "$Name-verify.txt")
$report
