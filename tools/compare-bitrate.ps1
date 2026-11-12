<#
.SYNOPSIS
  Accuracy cross-check: compares each destination's average bitrate computed
  from the dock's own byte counters (tick log) with the bytes the loopback
  MediaMTX server actually received for the same stream path, over the first
  stable streaming window (before any outage). Writes bitrate-check.txt.
#>
param(
    [Parameter(Mandatory)] [string]$ResultDir,
    [hashtable]$Map = @{ 'live/primary' = 'primary'; 'live/aitumA' = 'aitum:Loopback A'; 'live/aitumB' = 'aitum:Loopback B' },
    [double]$MaxDeviationPct = 5.0
)
$ErrorActionPreference = 'Stop'
$csv = Import-Csv (Join-Path $ResultDir 'mediamtx-bytes.csv')
$lines = Get-Content (Join-Path $ResultDir 'obs.log')

function ToSec([string]$hms) { [TimeSpan]::Parse($hms).TotalSeconds }

# Dock samples: key -> list of (t, bytes)
$dock = @{}
foreach ($l in $lines) {
    if ($l -match '^(\d\d:\d\d:\d\d\.\d+): \[multistream-stats\] tick row key="([^"]+)" .*? bytes=(\d+)') {
        if (-not $dock.ContainsKey($Matches[2])) { $dock[$Matches[2]] = New-Object System.Collections.Generic.List[object] }
        $dock[$Matches[2]].Add([pscustomobject]@{ t = (ToSec $Matches[1]); b = [double]$Matches[3] })
    }
}

$report = @("Bitrate cross-check (dock byte counters vs MediaMTX bytesReceived)")
$allPass = $true
foreach ($path in $Map.Keys) {
    $key = $Map[$path]
    # First monotonic run of MediaMTX samples for this path (one publish session).
    $s = @($csv | Where-Object { $_.path -eq $path } | ForEach-Object { [pscustomobject]@{ t = (ToSec $_.time); b = [double]$_.bytesReceived } })
    $run = New-Object System.Collections.Generic.List[object]
    foreach ($p in $s) { if ($run.Count -eq 0 -or $p.b -ge $run[$run.Count - 1].b) { $run.Add($p) } else { break } }
    if ($run.Count -lt 3 -or -not $dock.ContainsKey($key)) { $report += "  {0,-18} insufficient samples" -f $key; $allPass = $false; continue }
    # Skip the first sample (connection ramp-up).
    $t1 = $run[1].t; $t2 = $run[$run.Count - 1].t
    $mtxKbps = ($run[$run.Count - 1].b - $run[1].b) * 8 / ($t2 - $t1) / 1000
    $d = @($dock[$key] | Where-Object { $_.t -ge $t1 -and $_.t -le $t2 })
    if ($d.Count -lt 2) { $report += "  {0,-18} no dock samples in window" -f $key; $allPass = $false; continue }
    $dockKbps = ($d[$d.Count - 1].b - $d[0].b) * 8 / ($d[$d.Count - 1].t - $d[0].t) / 1000
    $dev = [math]::Abs($dockKbps - $mtxKbps) / $mtxKbps * 100
    $ok = $dev -le $MaxDeviationPct
    if (-not $ok) { $allPass = $false }
    $report += "  {0,-18} window {1,5:N1}s  dock {2,7:N1} kb/s  server {3,7:N1} kb/s  deviation {4,5:N2}%  {5}" -f $key, ($t2 - $t1), $dockKbps, $mtxKbps, $dev, $(if ($ok) { 'OK' } else { 'FAIL' })
}
$report += "RESULT: " + $(if ($allPass) { "PASS (all within $MaxDeviationPct%)" } else { 'FAIL' })
$report | Set-Content -Encoding utf8 (Join-Path $ResultDir 'bitrate-check.txt')
$report
