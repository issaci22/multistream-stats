<#
.SYNOPSIS
  Builds an ISOLATED, portable OBS test copy inside the workspace:
    test-env\obs\     copy of the installed OBS binaries (read-only source),
                      a fresh portable config (no stream keys, no audio capture),
                      the built plugin (OBS 33 layout) and the test-only mock plugin
    test-env\tools\mediamtx\   loopback-only RTMP server config

  The real OBS installation is only READ. Nothing is written outside the
  workspace. Run tools\build.ps1 -Preset windows-x64-dev first.

.PARAMETER WithAitum  Also copy Aitum Multistream (DLL + data) and give it a
                      test config whose destinations point only at 127.0.0.1.
.PARAMETER Fresh      Delete and recreate test-env\obs.
#>
param(
    [string]$ObsRoot = "$env:USERPROFILE\Documents\OBS Studio",
    [switch]$WithAitum,
    [switch]$Fresh
)
$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1's "Set-Content -Encoding utf8" writes a BOM, which
# MediaMTX's YAML parser rejects; write every generated file as UTF-8 w/o BOM.
function Write-NoBom {
    param([Parameter(Position = 0)] [string]$Path, [Parameter(ValueFromPipeline = $true)] $InputObject)
    begin { $sb = New-Object System.Text.StringBuilder }
    process { if ($null -ne $InputObject) { [void]$sb.Append([string]$InputObject) } }
    end { [IO.File]::WriteAllText($Path, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false)) }
}

$root = Split-Path -Parent $PSScriptRoot
$env_ = Join-Path $root 'test-env'
$obs = Join-Path $env_ 'obs'
$rundir = Join-Path $root 'build_x64\rundir\RelWithDebInfo'

foreach ($p in @("$ObsRoot\bin\64bit\obs64.exe", "$rundir\obs-multistream-stats\obs-multistream-stats.dll", "$rundir\msd-mock-outputs\msd-mock-outputs.dll")) {
    if (-not (Test-Path $p)) { throw "Missing: $p" }
}
if ($Fresh -and (Test-Path $obs)) { Remove-Item -Recurse -Force $obs }
New-Item -ItemType Directory -Force $obs | Out-Null

# --- 1. OBS binaries (copy only; source untouched) ---------------------------
# OBS 33 loads a fixed set of core modules from core\ (obs_load_core_modules);
# a missing one aborts loading of the rest, so every core module is copied.
$skipCore = @()
if ((Test-Path "$obs\core") -and ((Get-ChildItem "$obs\core" -Directory).Count -lt (Get-ChildItem "$ObsRoot\core" -Directory).Count)) {
    Remove-Item -Recurse -Force "$obs\bin", "$obs\core", "$obs\data" -ErrorAction SilentlyContinue
}
if (-not (Test-Path "$obs\bin\64bit\obs64.exe")) {
    Write-Host '== copying OBS binaries (bin, data, core)'
    Copy-Item -Recurse -Force "$ObsRoot\bin" "$obs\bin"
    New-Item -ItemType Directory -Force "$obs\data" | Out-Null
    foreach ($d in 'libobs', 'obs-studio', 'obs-scripting') { Copy-Item -Recurse -Force "$ObsRoot\data\$d" "$obs\data\$d" }
    New-Item -ItemType Directory -Force "$obs\core" | Out-Null
    Get-ChildItem "$ObsRoot\core" -Directory | Where-Object { $skipCore -notcontains $_.Name } |
        ForEach-Object { Copy-Item -Recurse -Force $_.FullName "$obs\core\$($_.Name)" }
}
Set-Content -Path "$obs\obs_portable_mode.txt" -Value '' -Encoding ascii

# --- 2. Plugins under test (OBS 33 layout) -----------------------------------
New-Item -ItemType Directory -Force "$obs\plugins" | Out-Null
foreach ($p in 'obs-multistream-stats', 'msd-mock-outputs') {
    if (Test-Path "$obs\plugins\$p") { Remove-Item -Recurse -Force "$obs\plugins\$p" }
    Copy-Item -Recurse -Force "$rundir\$p" "$obs\plugins\$p"
}

# --- 3. Optional: Aitum Multistream copy -------------------------------------
$aitumDll = "$obs\obs-plugins\64bit\aitum-multistream.dll"
if ($WithAitum) {
    New-Item -ItemType Directory -Force "$obs\obs-plugins\64bit", "$obs\data\obs-plugins" | Out-Null
    Copy-Item -Force "$ObsRoot\obs-plugins\64bit\aitum-multistream.dll", "$ObsRoot\obs-plugins\64bit\aitum-multistream.pdb" "$obs\obs-plugins\64bit\"
    Copy-Item -Recurse -Force "$ObsRoot\data\obs-plugins\aitum-multistream" "$obs\data\obs-plugins\aitum-multistream"
} elseif (Test-Path $aitumDll) {
    Remove-Item -Force "$obs\obs-plugins\64bit\aitum-multistream.*"
}

# --- 4. Fresh portable configuration -----------------------------------------
$cfg = Join-Path $obs 'config\obs-studio'
if (Test-Path $cfg) { Remove-Item -Recurse -Force $cfg }
$profileDir = Join-Path $cfg 'basic\profiles\MSD_Test'
$scenesDir = Join-Path $cfg 'basic\scenes'
New-Item -ItemType Directory -Force $profileDir, $scenesDir, "$env_\recordings" | Out-Null

@"
[General]
MaxLogs=20
InfoIncrement=-1
EnableAutoUpdates=false
LastVersion=553648129
Pre33.0Migrated=true

[Video]
Renderer=Direct3D 11
"@ | Write-NoBom "$cfg\global.ini"

@"
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

[Appearance]
FontScale=10
Density=1

[Basic]
Profile=MSD Test
ProfileDir=MSD_Test
SceneCollection=MSD Test
SceneCollectionFile=MSD_Test.json
ConfigOnNewProfile=false
"@ | Write-NoBom "$cfg\user.ini"

$recPath = ("$env_\recordings").Replace('\', '/')
@"
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
FilePath=$recPath
VBitrate=1000
ABitrate=128
StreamEncoder=x264
Preset=veryfast
"@ | Write-NoBom "$profileDir\basic.ini"

# Primary stream goes to the loopback server only (placeholder key).
'{"type":"rtmp_custom","settings":{"server":"rtmp://127.0.0.1:1935/live","key":"primary","use_auth":false,"bwtest":false}}' |
    Write-NoBom "$profileDir\service.json"

# Scene collection: one colour source, deliberately NO desktop/mic audio
# sources, so the test instance never captures the user's audio devices.
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
$scene | ConvertTo-Json -Depth 10 | Write-NoBom "$scenesDir\MSD_Test.json"

if ($WithAitum) {
    $aitumCfg = Join-Path $cfg 'plugin_config\aitum-multistream'
    New-Item -ItemType Directory -Force $aitumCfg | Out-Null
    $dest = { param($name, $key, $kbps) @{ name = $name; stream_server = 'rtmp://127.0.0.1:1935/live'; stream_key = $key; advanced = $true
        video_encoder = 'obs_x264'; video_encoder_settings = @{ bitrate = $kbps; rate_control = 'CBR'; preset = 'veryfast'; keyint_sec = 2 }
        audio_encoder = 'ffmpeg_aac'; audio_encoder_settings = @{ bitrate = 128 }; scale_type = 3 } }
    @{ profiles = @(@{ name = 'MSD Test'; outputs = @((& $dest 'Loopback A' 'aitumA' 900), (& $dest 'Loopback B' 'aitumB' 600)) }) } |
        ConvertTo-Json -Depth 10 | Write-NoBom "$aitumCfg\config.json"
}

# --- 5. Loopback-only MediaMTX config ----------------------------------------
$mtx = Join-Path $env_ 'tools\mediamtx\bin'
if (Test-Path "$mtx\mediamtx.yml") {
    $yml = Get-Content "$mtx\mediamtx.yml" -Raw
    # Only RTMP (ingest) and the API (byte counters) are enabled, every
    # protocol that exists in this MediaMTX version is switched off, and every
    # listen address is pinned to the loopback interface.
    $set = @{ 'api' = 'true'; 'rtsp' = 'false'; 'rtmp' = 'true'; 'hls' = 'false'; 'webrtc' = 'false'; 'srt' = 'false'
              'moq' = 'false'; 'metrics' = 'false'; 'pprof' = 'false'; 'playback' = 'false' }
    foreach ($k in $set.Keys) { $yml = [regex]::Replace($yml, "(?m)^$k\:.*$", "${k}: $($set[$k])") }
    $yml = [regex]::Replace($yml, '(?m)^(\w+Address): :(\d+)\s*$', '$1: 127.0.0.1:$2')
    Write-NoBom "$env_\tools\mediamtx\mediamtx-loopback.yml" -InputObject $yml
}

Write-Host "== test environment ready: $obs (Aitum: $([bool]$WithAitum))"
