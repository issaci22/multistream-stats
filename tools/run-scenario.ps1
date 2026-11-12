<#
.SYNOPSIS
  Runs one scripted scenario in the ISOLATED test OBS (test-env\obs) and
  evaluates its expectations against the dock's machine-readable tick log.

  Results go to test-env\results\<name>\ : obs.log, result.json, summary.txt,
  screenshots, mediamtx.log and mediamtx-bytes.csv (when -WithMediaMTX).

.PARAMETER Scenario      Path to a scenario JSON (tests\scenarios\*.json).
.PARAMETER ScaleFactor   Optional QT_SCALE_FACTOR for this OBS process only (DPI tests).
.PARAMETER WithMediaMTX  Start the loopback-only RTMP server for this run.
.PARAMETER ExtraEnv      Extra environment variables for this OBS process only
                         (e.g. @{ MSD_MOCK_STARTUP_LAG_MS = '100' }).
.PARAMETER FreshDockState Remove the saved dock layout (DockState) from the TEST
                         copy's user.ini first, like a first-time user (only
                         the test copy's config is touched).
.PARAMETER ObsDir        Another isolated portable OBS copy inside test-env\ (default
                         test-env\obs), e.g. the installer test copies.
#>
param(
    [Parameter(Mandatory)] [string]$Scenario,
    [string]$Name = '',
    [string]$ScaleFactor = '',
    [switch]$WithMediaMTX,
    [int]$TimeoutSec = 0,
    [hashtable]$ExtraEnv = @{},
    [switch]$FreshDockState,
    [string]$ObsDir = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$envDir = Join-Path $root 'test-env'
if (-not $ObsDir) { $ObsDir = Join-Path $envDir 'obs' }
$ObsDir = [IO.Path]::GetFullPath($ObsDir).TrimEnd('\')
if (-not $ObsDir.StartsWith("$envDir\", [StringComparison]::OrdinalIgnoreCase)) { throw "ObsDir must be an isolated copy inside $envDir" }
$obsBin = Join-Path $ObsDir 'bin\64bit'
$cfg = Join-Path $ObsDir 'config\obs-studio'
if (-not (Test-Path "$obsBin\obs64.exe")) { throw 'Run tools\setup-test-env.ps1 first.' }
$Scenario = (Resolve-Path $Scenario).Path
$spec = Get-Content $Scenario -Raw | ConvertFrom-Json
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($Scenario) + $(if ($ScaleFactor) { "-scale$ScaleFactor" } else { '' }) }
$out = Join-Path $envDir "results\$Name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

$lastAt = ($spec.steps | ForEach-Object { [double]$_.at } | Measure-Object -Maximum).Maximum
if ($TimeoutSec -le 0) { $TimeoutSec = [int]$lastAt + 90 }

# Snapshot crash dumps so new ones can be detected.
$crashDir = Join-Path $cfg 'crashes'
$crashesBefore = @(if (Test-Path $crashDir) { Get-ChildItem $crashDir -File | ForEach-Object Name })

# --- MediaMTX (loopback only) ------------------------------------------------
$mtx = $null
$mtxJob = $null
$mtxRuns = 0
function Start-Mtx {
    $script:mtxRuns++
    $exe = Join-Path $envDir 'tools\mediamtx\bin\mediamtx.exe'
    $yml = Join-Path $envDir 'tools\mediamtx\mediamtx-loopback.yml'
    $script:mtx = Start-Process -FilePath $exe -ArgumentList "`"$yml`"" -PassThru -WindowStyle Hidden -WorkingDirectory (Split-Path $exe) `
        -RedirectStandardOutput (Join-Path $out "mediamtx-$($script:mtxRuns).log") -RedirectStandardError (Join-Path $out "mediamtx-$($script:mtxRuns).err.log")
}
if ($WithMediaMTX) {
    Start-Mtx
    Start-Sleep -Seconds 2
    $mtxJob = Start-Job -ArgumentList (Join-Path $out 'mediamtx-bytes.csv') -ScriptBlock {
        param($csv)
        'time,path,bytesReceived' | Set-Content $csv
        while ($true) {
            try {
                $r = Invoke-RestMethod -Uri 'http://127.0.0.1:9997/v3/paths/list' -TimeoutSec 2
                foreach ($i in $r.items) { "{0},{1},{2}" -f (Get-Date).ToString('HH:mm:ss.fff'), $i.name, $i.bytesReceived | Add-Content $csv }
            } catch {}
            Start-Sleep -Seconds 2
        }
    }
}

# --- Launch isolated OBS ------------------------------------------------------
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $obsBin 'obs64.exe'
$psi.WorkingDirectory = $obsBin
$psi.Arguments = '--portable --multi --disable-updater --disable-shutdown-check --disable-missing-files-check'
$psi.UseShellExecute = $false
$psi.EnvironmentVariables['MSD_DEBUG_TICKS'] = '1'
$psi.EnvironmentVariables['MSD_MOCK_SCENARIO'] = $Scenario
$psi.EnvironmentVariables['MSD_TEST_OUT'] = $out
if ($ScaleFactor) { $psi.EnvironmentVariables['QT_SCALE_FACTOR'] = $ScaleFactor }
foreach ($k in $ExtraEnv.Keys) { $psi.EnvironmentVariables[$k] = [string]$ExtraEnv[$k] }
if ($FreshDockState) {
    $userIni = Join-Path $cfg 'user.ini'
    if ($userIni -notlike '*\test-env\*\config\*') { throw "refusing to edit $userIni" }
    if (Test-Path $userIni) {
        $kept = Get-Content $userIni | Where-Object { $_ -notmatch '^DockState=' }
        [IO.File]::WriteAllLines($userIni, [string[]]$kept, (New-Object System.Text.UTF8Encoding($false)))
    }
}
$started = Get-Date
$proc = [System.Diagnostics.Process]::Start($psi)
$deadline = $started.AddSeconds($TimeoutSec)

# Timed MediaMTX outages, aligned to the harness timeline start.
$mtxEvents = @($spec.mediamtx_events | Where-Object { $_ })
if ($WithMediaMTX -and $mtxEvents.Count -gt 0) {
    $t0 = $null
    while (-not $t0 -and (Get-Date) -lt $deadline -and -not $proc.HasExited) {
        $lf = Get-ChildItem (Join-Path $cfg 'logs') -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($lf -and (Select-String -LiteralPath $lf.FullName -Pattern '\[msd-mock\] timeline started' -Quiet)) { $t0 = Get-Date }
        else { Start-Sleep -Milliseconds 200 }
    }
    foreach ($ev in ($mtxEvents | Sort-Object { [double]$_.at })) {
        if (-not $t0) { break }
        $when = $t0.AddSeconds([double]$ev.at)
        while ((Get-Date) -lt $when -and -not $proc.HasExited) { Start-Sleep -Milliseconds 100 }
        if ($proc.HasExited) { break }
        if ($ev.action -eq 'stop' -and $mtx -and -not $mtx.HasExited) { Stop-Process -Id $mtx.Id -Force }
        elseif ($ev.action -eq 'start') { Start-Mtx }
        "{0} mediamtx {1} (timeline +{2}s)" -f (Get-Date).ToString('HH:mm:ss.fff'), $ev.action, $ev.at | Add-Content (Join-Path $out 'mediamtx-events.txt')
    }
}

$remainingMs = [int][Math]::Max(1000, ($deadline - (Get-Date)).TotalMilliseconds)
$exited = $proc.WaitForExit($remainingMs)
if (-not $exited) { $proc.Kill(); $proc.WaitForExit() }
$exitCode = $proc.ExitCode

if ($mtxJob) { Stop-Job $mtxJob; Remove-Job $mtxJob }
if ($mtx -and -not $mtx.HasExited) { Stop-Process -Id $mtx.Id -Force }

# --- Collect log ---------------------------------------------------------------
$log = Get-ChildItem (Join-Path $cfg 'logs') -File | Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) { throw 'No OBS log produced.' }
Copy-Item $log.FullName (Join-Path $out 'obs.log')
$lines = Get-Content $log.FullName

function ConvertFrom-TickLine([string]$line) {
    $h = @{}
    foreach ($m in [regex]::Matches($line, '(\w+)=("([^"]*)"|\S+)')) {
        $v = if ($m.Groups[3].Success) { $m.Groups[3].Value } else { $m.Groups[2].Value }
        $h[$m.Groups[1].Value] = $v
    }
    return $h
}

function Test-Value([string]$actual, [string]$expected) {
    if ($expected -eq 'na') { return $actual -eq 'na' }
    if ($expected -eq '!na') { return $actual -ne 'na' -and $null -ne $actual }
    if ($null -eq $actual -or $actual -eq 'na') { return $false }
    if ($expected -match '^(>=|<=|>|<)(-?[\d.]+)$') {
        $a = [double]$actual; $b = [double]$Matches[2]
        switch ($Matches[1]) { '>' { return $a -gt $b } '<' { return $a -lt $b } '>=' { return $a -ge $b } '<=' { return $a -le $b } }
    }
    return $actual -eq $expected
}

# First complete tick block after a checkpoint.
function Get-TickBlock([string]$checkpoint) {
    $idx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "\[msd-mock\] CHECKPOINT $([regex]::Escape($checkpoint))\s*$") { $idx = $i; break } }
    if ($idx -lt 0) { return $null }
    $rows = @{}
    for ($j = $idx + 1; $j -lt $lines.Count; $j++) {
        if ($lines[$j] -match '\[multistream-stats\] tick row ') { $f = ConvertFrom-TickLine $lines[$j]; $rows[$f['key']] = $f }
        elseif ($lines[$j] -match '\[multistream-stats\] tick summary ') { return @{ rows = $rows; summary = (ConvertFrom-TickLine $lines[$j]); line = $j } }
    }
    return $null
}

$results = New-Object System.Collections.Generic.List[object]
foreach ($step in ($spec.steps | Where-Object { $_.action -eq 'checkpoint' -and $_.expect })) {
    $block = Get-TickBlock $step.name
    if (-not $block) { $results.Add([pscustomobject]@{ checkpoint = $step.name; target = '-'; field = '-'; expected = 'tick block'; actual = 'missing'; pass = $false }); continue }
    if ($step.expect.rows) {
        foreach ($rk in $step.expect.rows.PSObject.Properties) {
            $row = $block.rows[$rk.Name]
            foreach ($fe in $rk.Value.PSObject.Properties) {
                if ($fe.Name -eq 'absent') {
                    $ok = ($null -eq $row) -eq [bool]$fe.Value
                    $results.Add([pscustomobject]@{ checkpoint = $step.name; target = $rk.Name; field = 'absent'; expected = "$($fe.Value)"; actual = "$($null -eq $row)"; pass = $ok }); continue
                }
                $actual = if ($row) { $row[$fe.Name] } else { $null }
                $results.Add([pscustomobject]@{ checkpoint = $step.name; target = $rk.Name; field = $fe.Name; expected = "$($fe.Value)"; actual = "$actual"; pass = (Test-Value $actual "$($fe.Value)") })
            }
        }
    }
    if ($step.expect.summary) {
        foreach ($fe in $step.expect.summary.PSObject.Properties) {
            $actual = $block.summary[$fe.Name]
            $results.Add([pscustomobject]@{ checkpoint = $step.name; target = 'summary'; field = $fe.Name; expected = "$($fe.Value)"; actual = "$actual"; pass = (Test-Value $actual "$($fe.Value)") })
        }
    }
}

# Probe steps (1.0.1): "[msd-mock] PROBE <kind> name=<name> key=value ..." lines
# evaluated against the step's "expect" object.
foreach ($step in ($spec.steps | Where-Object { $_.action -like 'probe_*' -and $_.expect })) {
    $kind = $step.action.Substring(6)
    $probe = $lines | Where-Object { $_ -match "\[msd-mock\] PROBE $kind name=$([regex]::Escape($step.name)) " } | Select-Object -First 1
    if (-not $probe) { $results.Add([pscustomobject]@{ checkpoint = $step.name; target = "probe_$kind"; field = '-'; expected = 'probe line'; actual = 'missing'; pass = $false }); continue }
    $f = ConvertFrom-TickLine $probe
    foreach ($fe in $step.expect.PSObject.Properties) {
        $actual = $f[$fe.Name]
        $results.Add([pscustomobject]@{ checkpoint = $step.name; target = "probe_$kind"; field = $fe.Name; expected = "$($fe.Value)"; actual = "$actual"; pass = (Test-Value $actual "$($fe.Value)") })
    }
}

# Log-line expectations: { "log_expect": [ { "pattern": "regex", "count": ">=1" } ] }
foreach ($le in @($spec.log_expect | Where-Object { $_ })) {
    $n = @($lines | Where-Object { $_ -match $le.pattern }).Count
    $results.Add([pscustomobject]@{ checkpoint = 'log'; target = $le.pattern; field = 'count'; expected = "$($le.count)"; actual = "$n"; pass = (Test-Value "$n" "$($le.count)") })
}

# Global checks.
$text = $lines -join "`n"
$leaks = if ($text -match 'Number of memory leaks: (\d+)') { [int]$Matches[1] } else { -1 }
$crashesAfter = @(if (Test-Path $crashDir) { Get-ChildItem $crashDir -File | ForEach-Object Name })
$newCrashes = @($crashesAfter | Where-Object { $crashesBefore -notcontains $_ })
$global = [ordered]@{
    exited_cleanly       = $exited
    exit_code            = $exitCode
    plugin_loaded        = [bool]($text -match '\[multistream-stats\] loaded version')
    monitoring_started   = [bool]($text -match '\[multistream-stats\] monitoring started')
    monitoring_stopped   = [bool]($text -match '\[multistream-stats\] monitoring stopped')
    plugin_unloaded      = [bool]($text -match '\[multistream-stats\] unloaded')
    plugin_warnings      = @($lines | Where-Object { $_ -match '\[multistream-stats\]' -and $_ -match '(?i)warning|error|fail' -and $_ -notmatch 'tick (row|summary)' }).Count
    memory_leaks         = $leaks
    new_crash_dumps      = $newCrashes.Count
    tick_timing          = (($lines | Where-Object { $_ -match '\[multistream-stats\] statistics ticks' }) -replace '^.*statistics ticks: ', '')
}
# Leak baseline: measured by a control run of the same OBS copy WITHOUT this
# plugin (test-env\leak-baseline.txt); the plugin must not add any leaks.
$baselineFile = Join-Path $envDir "leak-baseline-$([IO.Path]::GetFileNameWithoutExtension($Scenario)).txt"
if (-not (Test-Path $baselineFile)) { $baselineFile = Join-Path $envDir 'leak-baseline.txt' }
$leakBaseline = if (Test-Path $baselineFile) { [int](Get-Content $baselineFile -Raw).Trim() } else { 0 }
$global['memory_leak_baseline'] = $leakBaseline
$globalPass = $global.exited_cleanly -and $global.plugin_loaded -and $global.monitoring_started -and $global.monitoring_stopped -and
              $global.plugin_unloaded -and $global.memory_leaks -ge 0 -and $global.memory_leaks -le $leakBaseline -and
              $global.new_crash_dumps -eq 0 -and $global.plugin_warnings -eq 0
$failed = @($results | Where-Object { -not $_.pass })

[ordered]@{ scenario = $Scenario; name = $Name; scale = $ScaleFactor; started = $started.ToString('o'); global = $global
    global_pass = $globalPass; checks = $results; failed = $failed.Count } | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $out 'result.json')

$summary = @("Scenario $Name  ($((Get-Date) - $started | ForEach-Object { '{0:N0}s' -f $_.TotalSeconds }))")
$summary += ($global.GetEnumerator() | ForEach-Object { "  {0,-20} {1}" -f $_.Key, $_.Value })
$summary += "  checks: $($results.Count - $failed.Count)/$($results.Count) passed"
$summary += ($failed | ForEach-Object { "  FAIL [$($_.checkpoint)] $($_.target).$($_.field): expected $($_.expected), got $($_.actual)" })
$summary += "RESULT: " + $(if ($globalPass -and $failed.Count -eq 0) { 'PASS' } else { 'FAIL' })
$summary | Set-Content -Encoding utf8 (Join-Path $out 'summary.txt')
$summary
