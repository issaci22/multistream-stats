<#
.SYNOPSIS
  Full regression: build + unit tests, ABI check against the installed OBS,
  isolated OBS scenarios (mock outputs, DPI scales, real Aitum + loopback
  RTMP), bitrate cross-check, and verification that the real OBS folder is
  unchanged. Summary: logs\test-summary.txt

  Requires the user's approval for isolated live tests (already granted for
  this project): it launches the isolated OBS copy in test-env\ and a
  loopback-only MediaMTX. Nothing is streamed to any platform.
#>
param(
    [switch]$SkipLive,
    # logs\<name>.json baseline of the real OBS folder to verify against
    # (tools\obs-baseline.ps1 -Mode Capture -Name <name>, taken with OBS closed).
    [string]$Baseline = 'obs-baseline'
)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$t = Join-Path $root 'tools'
$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("Test run $(Get-Date -Format s)")

function Add-Result([string]$name, [bool]$ok, [string]$detail = '') {
    $script:summary.Add(("{0,-34} {1}  {2}" -f $name, $(if ($ok) { 'PASS' } else { 'FAIL' }), $detail))
}

& "$t\build.ps1" -Preset windows-x64-dev -Test *> (Join-Path $root 'logs\run-all-build.log')
$unit = Select-String -Path (Join-Path $root 'logs\test-windows-x64-dev.log') -Pattern '100% tests passed' -Quiet
$doctest = & (Join-Path $root 'build_x64\tests\unit\RelWithDebInfo\msd-core-tests.exe') --no-intro=true 2>&1 | Select-Object -Last 3
Add-Result 'build + unit tests' $unit (($doctest | Where-Object { $_ -match 'test cases' }) -replace '\s+', ' ')

$abi = & "$t\check-abi.ps1" 2>&1 | Select-Object -Last 1
Add-Result 'ABI check vs installed OBS' ($abi -match 'PASS') "$abi"

if (-not $SkipLive) {
    & "$t\setup-test-env.ps1" *> $null
    foreach ($run in @(@{ s = 'smoke' }, @{ s = 'mock-lifecycle' }, @{ s = 'ui-scale'; f = '1' }, @{ s = 'ui-scale'; f = '1.5' }, @{ s = 'ui-scale'; f = '2' })) {
        $args_ = @{ Scenario = (Join-Path $root "tests\scenarios\$($run.s).json") }
        if ($run.f) { $args_['ScaleFactor'] = $run.f }
        $res = & "$t\run-scenario.ps1" @args_ 2>&1 | ForEach-Object { "$_" }
        $name = $run.s + $(if ($run.f) { " @$($run.f)x" } else { '' })
        Add-Result "isolated: $name" (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    }
    # 1.0.1: narrow layout at three DPI scales, first-open width, lag window vs native Stats.
    foreach ($f in '1', '1.5', '2') {
        $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\narrow-layout.json') -ScaleFactor $f 2>&1 | ForEach-Object { "$_" }
        Add-Result "isolated: narrow-layout @${f}x" (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    }
    # 1.1.0: active-only output list, responsive (tall / wide) layout at three DPI scales.
    $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\active-only.json') 2>&1 | ForEach-Object { "$_" }
    Add-Result 'isolated: active-only' (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    foreach ($f in '1', '1.5', '2') {
        $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\responsive-layout.json') -ScaleFactor $f 2>&1 | ForEach-Object { "$_" }
        Add-Result "isolated: responsive-layout @${f}x" (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    }
    $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\first-open.json') -FreshDockState 2>&1 | ForEach-Object { "$_" }
    Add-Result 'isolated: first-open (360 px)' (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\lag-window.json') -ExtraEnv @{ MSD_MOCK_STARTUP_LAG_MS = '300'; MSD_MOCK_STARTUP_LAG_TICKS = '12' } 2>&1 | ForEach-Object { "$_" }
    $probe = Select-String -Path (Join-Path $root 'test-env\results\lag-window\obs.log') -Pattern 'PROBE lag ' -ErrorAction SilentlyContinue | Select-Object -First 1
    $pd = if ($probe -and $probe.Line -match 'diff_total=(\S+) .*diff_lagged=(\S+) ') { "window diff: $($Matches[1]) frames, $($Matches[2]) lagged" } else { '' }
    Add-Result 'isolated: lag-window vs native' (($res | Select-Object -Last 1) -match 'PASS') ((($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ') + "  $pd")

    & "$t\setup-test-env.ps1" -WithAitum *> $null
    $res = & "$t\run-scenario.ps1" -Scenario (Join-Path $root 'tests\scenarios\aitum-loopback.json') -WithMediaMTX 2>&1 | ForEach-Object { "$_" }
    Add-Result 'isolated: aitum-loopback (real)' (($res | Select-Object -Last 1) -match 'PASS') (($res | Where-Object { $_ -match 'checks:' }) -replace '\s+', ' ')
    $br = & "$t\compare-bitrate.ps1" -ResultDir (Join-Path $root 'test-env\results\aitum-loopback') 2>&1
    Add-Result 'bitrate vs MediaMTX' (($br | Select-Object -Last 1) -match 'PASS') ((($br | Where-Object { $_ -match 'deviation' }) | ForEach-Object { ($_ -split 'deviation')[1].Trim() }) -join ', ')
}

$verify = & "$t\obs-baseline.ps1" -Mode Verify -Name $Baseline 2>&1 | Select-Object -Last 1
Add-Result 'real OBS folder unchanged' ($verify -match 'PASS') "$verify"

$summary | Set-Content -Encoding utf8 (Join-Path $root 'logs\test-summary.txt')
$summary
