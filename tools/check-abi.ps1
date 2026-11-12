<#
.SYNOPSIS
  Load-compatibility check without starting OBS: verifies that every symbol the
  built plugin imports from OBS / Qt is exported by the DLLs of a given OBS
  installation (read-only). Writes logs\abi-check.txt.

.PARAMETER ObsRoot  OBS installation root (default: the user's portable OBS).
.PARAMETER Plugin   Built plugin DLL (default: RelWithDebInfo rundir).
#>
param(
    [string]$ObsRoot = "$env:USERPROFILE\Documents\OBS Studio",
    [string]$Plugin = ''
)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Plugin) { $Plugin = Join-Path $root 'build_x64\rundir\RelWithDebInfo\obs-multistream-stats\obs-multistream-stats.dll' }
if (-not (Test-Path $Plugin)) { throw "Plugin not found: $Plugin" }
$obsBin = Join-Path $ObsRoot 'bin\64bit'
if (-not (Test-Path $obsBin)) { throw "OBS bin directory not found: $obsBin" }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe |
    Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe not found (VS Build Tools C++ workload required).' }

function Get-Imports([string]$dll) {
    $map = @{}
    $current = $null
    foreach ($line in (& $dumpbin.FullName /nologo /imports $dll)) {
        if ($line -match '^\s+Summary\s*$') { break } # trailing section-size table
        if ($line -match '^\s{4}(\S+\.dll)\s*$') { $current = $Matches[1]; $map[$current] = New-Object System.Collections.Generic.List[string]; continue }
        if ($current -and $line -match '^\s+[0-9A-F]+\s+(\S+)\s*$') { $map[$current].Add($Matches[1]) }
    }
    return $map
}

function Get-Exports([string]$dll) {
    $set = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($line in (& $dumpbin.FullName /nologo /exports $dll)) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { [void]$set.Add($Matches[1]) }
        elseif ($line -match '^\s+\d+\s+[0-9A-F]+\s+(\S+)\s+\(forwarded') { [void]$set.Add($Matches[1]) }
    }
    return $set
}

$report = New-Object System.Collections.Generic.List[string]
$report.Add("ABI check $(Get-Date -Format s)")
$report.Add("plugin : $Plugin")
$report.Add("OBS    : $obsBin ($((Get-Item (Join-Path $obsBin 'obs64.exe')).VersionInfo.ProductVersion))")
$missingTotal = 0
$imports = Get-Imports $Plugin
foreach ($dll in ($imports.Keys | Sort-Object)) {
    $target = Join-Path $obsBin $dll
    if (-not (Test-Path $target)) {
        $report.Add(("{0,-36} {1,4} symbols  (system DLL, not shipped by OBS)" -f $dll, $imports[$dll].Count))
        continue
    }
    $exports = Get-Exports $target
    $missing = @($imports[$dll] | Where-Object { -not $exports.Contains($_) })
    $missingTotal += $missing.Count
    $report.Add(("{0,-36} {1,4} symbols  resolved={2}  missing={3}" -f $dll, $imports[$dll].Count, ($imports[$dll].Count - $missing.Count), $missing.Count))
    foreach ($m in $missing) { $report.Add("    MISSING: $m") }
}
$report.Add("RESULT: " + $(if ($missingTotal -eq 0) { 'PASS - all OBS/Qt imports resolve against the installed OBS' } else { "FAIL - $missingTotal unresolved imports" }))
New-Item -ItemType Directory -Force (Join-Path $root 'logs') | Out-Null
$report | Set-Content -Encoding utf8 (Join-Path $root 'logs\abi-check.txt')
$report
if ($missingTotal -ne 0) { exit 1 }
