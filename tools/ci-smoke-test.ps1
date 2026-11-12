#Requires -Version 7.2
<#
.SYNOPSIS
  Cross-platform plugin-loading smoke test for CI (PowerShell 7 on Linux, macOS, Windows).

  Starts an INSTALLED OBS Studio with the test-only mock/harness plugin (msd-mock-outputs) made
  available through OBS_PLUGINS_PATH, runs one scenario (tests/scenarios/*.json), lets the harness
  quit OBS, and evaluates the OBS log the same way tools/run-scenario.ps1 does on Windows:
    - the plugin loaded (optionally: exactly -ExpectVersion), its locale loaded, the dock registered,
      monitoring started/stopped, the plugin unloaded, OBS exited by itself, no crash;
    - every checkpoint/probe expectation of the scenario;
    - memory leaks not above -LeakBaseline (a control run without this plugin).
  The plugin under test must already be installed the way users install it (.deb, .tar.xz, .pkg, ZIP,
  installer), so this also proves that OBS finds it at that location.

  Use only on disposable CI machines or isolated OBS copies: it writes OBS configuration into -ConfigRoot.

.PARAMETER ObsCommand    OBS executable (Linux: obs; macOS: /Applications/OBS.app/Contents/MacOS/OBS).
.PARAMETER ConfigRoot    OBS configuration folder ("obs-studio"): logs are read from <ConfigRoot>/logs.
.PARAMETER PluginsPath   Folder for OBS_PLUGINS_PATH holding the mock plugin. Must contain "/test-env/"
                         (the harness refuses to run elsewhere).
.PARAMETER Scenario      Scenario JSON.
.PARAMETER Out           Results folder (obs.log, result.json, summary.txt).
.PARAMETER SeedConfig    Write a minimal isolated OBS configuration first (no first-run wizard, a colour
                         source only, no audio capture, stream target 127.0.0.1 placeholder).
.PARAMETER ControlRun    Do not expect this plugin; only record the memory-leak count (writes leaks.txt).
.PARAMETER ExpectVersion Expected "[multistream-stats] loaded version <x>".
.PARAMETER LeakBaseline  Highest acceptable "Number of memory leaks" (from a control run).
#>
param(
    [Parameter(Mandatory)] [string]$ObsCommand,
    [Parameter(Mandatory)] [string]$ConfigRoot,
    [Parameter(Mandatory)] [string]$PluginsPath,
    [Parameter(Mandatory)] [string]$Scenario,
    [Parameter(Mandatory)] [string]$Out,
    [string[]]$ObsArgs = @('--multi', '--disable-updater', '--disable-missing-files-check'),
    [switch]$SeedConfig,
    [switch]$ControlRun,
    [string]$ExpectVersion = '',
    [int]$LeakBaseline = 0,
    [int]$TimeoutSec = 0
)
$ErrorActionPreference = 'Stop'

function Write-NoBom([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false))
}

$Scenario = (Resolve-Path $Scenario).Path
$spec = Get-Content $Scenario -Raw | ConvertFrom-Json
if (($PluginsPath -replace '\\', '/') -notlike '*/test-env/*') { throw "PluginsPath must contain /test-env/ (harness safety guard): $PluginsPath" }
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
$lastAt = ($spec.steps | ForEach-Object { [double]$_.at } | Measure-Object -Maximum).Maximum
if ($TimeoutSec -le 0) { $TimeoutSec = [int]$lastAt + 120 }

# --- Isolated configuration ------------------------------------------------------
if ($SeedConfig) {
    $profileDir = Join-Path $ConfigRoot 'basic/profiles/MSD_Test'
    $scenesDir = Join-Path $ConfigRoot 'basic/scenes'
    Write-NoBom (Join-Path $ConfigRoot 'global.ini') @"
[General]
MaxLogs=20
InfoIncrement=-1
EnableAutoUpdates=false
LastVersion=553648129
Pre33.0Migrated=true
MacOSPermissionsDialogLastShown=1
"@
    Write-NoBom (Join-Path $ConfigRoot 'user.ini') @"
[General]
Pre19Defaults=false
Pre21Defaults=false
Pre23Defaults=false
Pre24.1Defaults=false
ConfirmOnExit=false
FirstRun=true
YtDockCleanupDone=true
AutomaticCollectionSearch=false
AutoSearchPrompt=false

[BasicWindow]
SysTrayEnabled=false
PreviewEnabled=true
ShowStatusBar=true
WarnBeforeStartingStream=false
WarnBeforeStoppingStream=false

[Basic]
Profile=MSD Test
ProfileDir=MSD_Test
SceneCollection=MSD Test
SceneCollectionFile=MSD_Test.json
ConfigOnNewProfile=false
"@
    $rec = (Join-Path $Out 'recordings') -replace '\\', '/'
    Write-NoBom (Join-Path $profileDir 'basic.ini') @"
[General]
Name=MSD Test

[Video]
BaseCX=1280
BaseCY=720
OutputCX=1280
OutputCY=720
FPSType=0
FPSCommon=30

[Output]
Mode=Simple

[SimpleOutput]
FilePath=$rec
VBitrate=1000
ABitrate=128
StreamEncoder=x264
Preset=veryfast
"@
    Write-NoBom (Join-Path $profileDir 'service.json') '{"type":"rtmp_custom","settings":{"server":"rtmp://127.0.0.1:1935/live","key":"primary","use_auth":false,"bwtest":false}}'
    $scene = @{
        current_scene = 'Scene'; current_program_scene = 'Scene'; name = 'MSD Test'
        scene_order = @(@{ name = 'Scene' })
        sources = @(
            @{ name = 'Test Colour'; uuid = '7a9c0b6e-0f6e-4d55-9f62-6d1d5d2d0001'; id = 'color_source'; versioned_id = 'color_source_v3'
               settings = @{ color = 4283215696; width = 1280; height = 720 }; enabled = $true; volume = 1.0; mixers = 0 },
            @{ name = 'Scene'; uuid = '7a9c0b6e-0f6e-4d55-9f62-6d1d5d2d0002'; id = 'scene'; versioned_id = 'scene'
               settings = @{ id_counter = 1; custom_size = $false
                             items = @(@{ name = 'Test Colour'; source_uuid = '7a9c0b6e-0f6e-4d55-9f62-6d1d5d2d0001'; visible = $true; locked = $false
                                          rot = 0.0; pos = @{ x = 0.0; y = 0.0 }; scale = @{ x = 1.0; y = 1.0 }; alignment = 5; bounds_type = 0
                                          bounds_alignment = 0; bounds = @{ x = 0.0; y = 0.0 }; id = 1 }) }
               enabled = $true; mixers = 0 }
        )
        groups = @(); quick_transitions = @(); transitions = @(); saved_projectors = @()
        current_transition = 'Fade'; transition_duration = 300
    }
    Write-NoBom (Join-Path $scenesDir 'MSD_Test.json') ($scene | ConvertTo-Json -Depth 10)
}

# --- Run OBS -------------------------------------------------------------------------
$logDir = Join-Path $ConfigRoot 'logs'
$crashDir = Join-Path $ConfigRoot 'crashes'
$crashesBefore = @(if (Test-Path $crashDir) { Get-ChildItem $crashDir -File | ForEach-Object Name })
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ObsCommand
$psi.WorkingDirectory = Split-Path -Parent ((Get-Command $ObsCommand).Source)
foreach ($a in $ObsArgs) { [void]$psi.ArgumentList.Add($a) }
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.Environment['OBS_PLUGINS_PATH'] = $PluginsPath
$psi.Environment['MSD_DEBUG_TICKS'] = '1'
$psi.Environment['MSD_MOCK_SCENARIO'] = $Scenario
$psi.Environment['MSD_TEST_OUT'] = $Out
$started = Get-Date
$proc = [System.Diagnostics.Process]::Start($psi)
$stdout = $proc.StandardOutput.ReadToEndAsync()
$stderr = $proc.StandardError.ReadToEndAsync()
$exited = $proc.WaitForExit($TimeoutSec * 1000)
if (-not $exited) { $proc.Kill($true); $proc.WaitForExit() }
$exitCode = $proc.ExitCode
Write-NoBom (Join-Path $Out 'obs-stdout.txt') $stdout.Result
Write-NoBom (Join-Path $Out 'obs-stderr.txt') $stderr.Result

$log = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) { throw "No OBS log produced in $logDir (exit code $exitCode, exited: $exited). See obs-stderr.txt." }
Copy-Item $log.FullName (Join-Path $Out 'obs.log')
$lines = Get-Content $log.FullName
$text = $lines -join "`n"
$leaks = if ($text -match 'Number of memory leaks: (\d+)') { [int]$Matches[1] } else { -1 }
$crashesAfter = @(if (Test-Path $crashDir) { Get-ChildItem $crashDir -File | ForEach-Object Name })
$newCrashes = @($crashesAfter | Where-Object { $crashesBefore -notcontains $_ })

if ($ControlRun) {
    Write-NoBom (Join-Path $Out 'leaks.txt') "$leaks"
    $loadedOurs = [bool]($text -match '\[multistream-stats\] loaded version')
    $line = "control run: exited=$exited exit_code=$exitCode leaks=$leaks plugin_present=$loadedOurs timeout=${TimeoutSec}s duration=$('{0:N0}' -f ((Get-Date) - $started).TotalSeconds)s"
    Write-NoBom (Join-Path $Out 'summary.txt') "$line`n"
    $line
    if ($loadedOurs) { throw 'Control run must not load obs-multistream-stats' }
    if (-not $exited -or $leaks -lt 0) { throw 'Control run did not finish cleanly' }
    return
}

# --- Evaluate (same rules as tools/run-scenario.ps1) -------------------------------
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
function Get-TickBlock([string]$checkpoint) {
    $idx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "\[msd-mock\] CHECKPOINT $([regex]::Escape($checkpoint))\s*$") { $idx = $i; break } }
    if ($idx -lt 0) { return $null }
    $rows = @{}
    for ($j = $idx + 1; $j -lt $lines.Count; $j++) {
        if ($lines[$j] -match '\[multistream-stats\] tick row ') { $f = ConvertFrom-TickLine $lines[$j]; $rows[$f['key']] = $f }
        elseif ($lines[$j] -match '\[multistream-stats\] tick summary ') { return @{ rows = $rows; summary = (ConvertFrom-TickLine $lines[$j]) } }
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
                    $results.Add([pscustomobject]@{ checkpoint = $step.name; target = $rk.Name; field = 'absent'; expected = "$($fe.Value)"; actual = "$($null -eq $row)"; pass = (($null -eq $row) -eq [bool]$fe.Value) }); continue
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
foreach ($step in ($spec.steps | Where-Object { $_.action -like 'probe_*' -and $_.expect })) {
    $kind = $step.action.Substring(6)
    $probe = $lines | Where-Object { $_ -match "\[msd-mock\] PROBE $kind name=$([regex]::Escape($step.name)) " } | Select-Object -First 1
    if (-not $probe) { $results.Add([pscustomobject]@{ checkpoint = $step.name; target = "probe_$kind"; field = '-'; expected = 'probe line'; actual = 'missing'; pass = $false }); continue }
    $f = ConvertFrom-TickLine $probe
    foreach ($fe in $step.expect.PSObject.Properties) {
        $results.Add([pscustomobject]@{ checkpoint = $step.name; target = "probe_$kind"; field = $fe.Name; expected = "$($fe.Value)"; actual = "$($f[$fe.Name])"; pass = (Test-Value $f[$fe.Name] "$($fe.Value)") })
    }
}
foreach ($le in @($spec.log_expect | Where-Object { $_ })) {
    $n = @($lines | Where-Object { $_ -match $le.pattern }).Count
    $results.Add([pscustomobject]@{ checkpoint = 'log'; target = $le.pattern; field = 'count'; expected = "$($le.count)"; actual = "$n"; pass = (Test-Value "$n" "$($le.count)") })
}

$loadedLine = ($lines | Where-Object { $_ -match '\[multistream-stats\] loaded version' } | Select-Object -First 1)
$global = [ordered]@{
    exited_cleanly     = $exited
    exit_code          = $exitCode
    plugin_loaded      = [bool]$loadedLine
    loaded_line        = "$loadedLine".Trim()
    version_ok         = (-not $ExpectVersion) -or [bool]($text -match "\[multistream-stats\] loaded version $([regex]::Escape($ExpectVersion)) ")
    locale_ok          = -not [bool]($text -match "Failed to load '[^']*' text for module: '[^']*obs-multistream-stats")
    dock_registered    = -not [bool]($text -match "\[multistream-stats\] could not register dock")
    monitoring_started = [bool]($text -match '\[multistream-stats\] monitoring started')
    monitoring_stopped = [bool]($text -match '\[multistream-stats\] monitoring stopped')
    plugin_unloaded    = [bool]($text -match '\[multistream-stats\] unloaded')
    plugin_warnings    = @($lines | Where-Object { $_ -match '\[multistream-stats\]' -and $_ -match '(?i)warning|error|fail' -and $_ -notmatch 'tick (row|summary)' }).Count
    memory_leaks       = $leaks
    memory_leak_baseline = $LeakBaseline
    new_crash_dumps    = $newCrashes.Count
}
$globalPass = $global.exited_cleanly -and $global.plugin_loaded -and $global.version_ok -and $global.locale_ok -and $global.dock_registered -and
              $global.monitoring_started -and $global.monitoring_stopped -and $global.plugin_unloaded -and $global.plugin_warnings -eq 0 -and
              $global.memory_leaks -ge 0 -and $global.memory_leaks -le $LeakBaseline -and $global.new_crash_dumps -eq 0
$failed = @($results | Where-Object { -not $_.pass })
[ordered]@{ scenario = $Scenario; started = $started.ToString('o'); global = $global; global_pass = $globalPass; checks = $results; failed = $failed.Count } |
    ConvertTo-Json -Depth 6 | ForEach-Object { Write-NoBom (Join-Path $Out 'result.json') $_ }
$summary = @("Scenario $([IO.Path]::GetFileNameWithoutExtension($Scenario))  ($('{0:N0}s' -f ((Get-Date) - $started).TotalSeconds))")
$summary += ($global.GetEnumerator() | ForEach-Object { "  {0,-22} {1}" -f $_.Key, $_.Value })
$summary += "  checks: $($results.Count - $failed.Count)/$($results.Count) passed"
$summary += ($failed | ForEach-Object { "  FAIL [$($_.checkpoint)] $($_.target).$($_.field): expected $($_.expected), got $($_.actual)" })
$summary += 'RESULT: ' + $(if ($globalPass -and $failed.Count -eq 0) { 'PASS' } else { 'FAIL' })
Write-NoBom (Join-Path $Out 'summary.txt') (($summary -join "`n") + "`n")
$summary
if (-not ($globalPass -and $failed.Count -eq 0)) { exit 1 }
