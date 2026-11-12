<#
.SYNOPSIS
  Configure, build and (optionally) test / stage the plugin. Everything stays
  inside the workspace: .deps\ (OBS SDK + obs-deps + Qt), build_x64\, dist\.

.PARAMETER Preset
  windows-x64      plugin + unit tests (default)
  windows-x64-dev  additionally builds the test-only mock output plugin

.PARAMETER Test      Run ctest after building.
.PARAMETER Package   Stage the installable plugin folder into dist\ (cmake --install).
#>
param(
    [ValidateSet('windows-x64', 'windows-x64-dev')]
    [string]$Preset = 'windows-x64',
    [switch]$Test,
    [switch]$Package
)

# Native tools write warnings to stderr; failures are detected via $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Locate the CMake bundled with Visual Studio 2026 (Build Tools or IDE).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2026 (Build Tools) not found: vswhere.exe missing.' }
$vs = & $vswhere -latest -products * -version '[18.0,19.0)' -property installationPath
if (-not $vs) { throw 'Visual Studio 2026 (version 18.x) not found.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path $cmake)) { throw "CMake not found at $cmake (install the 'C++ CMake tools' component)." }

New-Item -ItemType Directory -Force (Join-Path $root 'logs') | Out-Null

Write-Host "== configure ($Preset)"
& $cmake --preset $Preset 2>&1 | Tee-Object -FilePath (Join-Path $root "logs\configure-$Preset.log") | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }

Write-Host "== build ($Preset)"
& $cmake --build --preset $Preset -- /nologo /v:minimal 2>&1 | Tee-Object -FilePath (Join-Path $root "logs\build-$Preset.log") |
    Where-Object { $_ -match 'error|warning' } | Select-Object -First 60
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE) - see logs\build-$Preset.log" }

if ($Test) {
    Write-Host "== test ($Preset)"
    & $ctest --preset $Preset 2>&1 | Tee-Object -FilePath (Join-Path $root "logs\test-$Preset.log")
    if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
}

if ($Package) {
    Write-Host '== stage dist\'
    $dist = Join-Path $root 'dist'
    if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
    & $cmake --install build_x64 --config RelWithDebInfo --prefix $dist 2>&1 | Select-Object -Last 5
    if ($LASTEXITCODE -ne 0) { throw "install to dist failed ($LASTEXITCODE)" }
    Get-ChildItem $dist -Recurse -File | ForEach-Object { $_.FullName.Substring($root.Length + 1) }
}
Write-Host '== done'
