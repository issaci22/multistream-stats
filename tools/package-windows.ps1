<#
.SYNOPSIS
  Builds the Windows release assets from a plugin payload folder and verifies them
  after packaging:
    obs-multistream-stats-<v>-windows-x64-manual.zip
    obs-multistream-stats-<v>-windows-x64-installer.exe   (Inno Setup 6.3+, packaging\windows\installer.iss)
    SHA256SUMS-windows.txt, package-info.json
  Only writes into -OutDir (default artifacts\<version>\windows). Never touches
  releases\, dist\ or any OBS folder. Debug symbols (.pdb) are never packaged.

.PARAMETER Payload        Folder with obs-multistream-stats.dll and data\ (a .pdb in it is ignored).
.PARAMETER OutDir         Output folder (must not be inside releases\ or dist\).
.PARAMETER Iscc           Path to ISCC.exe; searched in the usual places if omitted.
.PARAMETER SkipInstaller  Build only the ZIP.
.PARAMETER AllowUnpinned  Package a payload that is not listed (or differs) in
                          packaging\windows\known-good-payloads.json. For CI pipeline
                          checks only; such packages are never released.
.PARAMETER AppPublisher   Publisher shown by the installer (default: buildspec.json "author").
.PARAMETER ProjectUrl     Project page (default: buildspec.json "website"); used in INSTALL.txt
                          and the installer's support links.
.PARAMETER Force          Replace an existing, non-empty -OutDir.

.EXAMPLE
  tools\package-windows.ps1                                   # 1.1.0 from releases\1.1.0 (pinned)
  tools\package-windows.ps1 -Payload build_x64\stage\obs-multistream-stats -OutDir artifacts\ci\windows -AllowUnpinned
#>
param(
    [string]$Payload = 'releases\1.1.0\obs-multistream-stats',
    [string]$OutDir = '',
    [string]$Iscc = '',
    [switch]$SkipInstaller,
    [switch]$AllowUnpinned,
    [string]$AppPublisher = '',
    [string]$ProjectUrl = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$name = 'obs-multistream-stats'

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Get-BytesSha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { -join ($sha.ComputeHash($Bytes) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
}
function Write-Utf8NoBom([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false)) }
function ConvertTo-Crlf([string]$Text) { ($Text -replace "`r`n", "`n") -replace "`n", "`r`n" }

# --- 1. Payload ---------------------------------------------------------------
$payloadDir = (Resolve-Path -LiteralPath (Join-Path $root $Payload) -ErrorAction SilentlyContinue)
if (-not $payloadDir) { $payloadDir = Resolve-Path -LiteralPath $Payload }
$payloadDir = $payloadDir.Path.TrimEnd('\')
$files = @(Get-ChildItem -LiteralPath $payloadDir -Recurse -File | Where-Object { $_.Extension -ne '.pdb' })
$rel = @{}
foreach ($f in $files) { $rel[$f.FullName.Substring($payloadDir.Length + 1).Replace('\', '/')] = $f.FullName }
foreach ($required in "$name.dll", 'data/manifest.json') {
    if (-not $rel.ContainsKey($required)) { throw "Payload $payloadDir has no $required" }
}
$unexpected = @($rel.Keys | Where-Object { $_ -ne "$name.dll" -and $_ -notlike 'data/*' })
if ($unexpected) { throw "Unexpected files in payload: $($unexpected -join ', ')" }

$version = (Get-Content -Raw -LiteralPath $rel['data/manifest.json'] | ConvertFrom-Json).version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Bad version '$version' in manifest.json" }
$dllVersion = (Get-Item -LiteralPath $rel["$name.dll"]).VersionInfo.FileVersion
if ($dllVersion -and $dllVersion -ne $version) { throw "DLL file version $dllVersion does not match manifest version $version" }

$payloadHashes = [ordered]@{}
foreach ($k in ($rel.Keys | Sort-Object)) { $payloadHashes[$k] = Get-Sha256 $rel[$k] }

# --- 2. Known-good pin --------------------------------------------------------
$pins = Get-Content -Raw (Join-Path $root 'packaging\windows\known-good-payloads.json') | ConvertFrom-Json
$pin = $pins.PSObject.Properties[$version]
$pinned = $false
if ($pin) {
    $want = $pin.Value.files.PSObject.Properties
    $mismatch = @()
    foreach ($p in $want) { if ($payloadHashes[$p.Name] -ne $p.Value) { $mismatch += $p.Name } }
    $extra = @($payloadHashes.Keys | Where-Object { -not $pin.Value.files.PSObject.Properties[$_] })
    if (-not $mismatch -and -not $extra) { $pinned = $true }
    elseif (-not $AllowUnpinned) { throw "Payload $version differs from the tested files ($(@($mismatch + $extra) -join ', ')). Refusing to package it as $version." }
} elseif (-not $AllowUnpinned) {
    throw "Version $version is not listed in packaging\windows\known-good-payloads.json. Test it, add its hashes, or use -AllowUnpinned for a CI check."
}
Write-Host ("== payload {0} ({1})" -f $version, $(if ($pinned) { 'pinned: tested files' } else { 'UNPINNED: not for release' }))

# --- 3. Output folder ---------------------------------------------------------
if (-not $OutDir) { $OutDir = Join-Path $root "artifacts\$version\windows" }
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$OutDir = [IO.Path]::GetFullPath($OutDir).TrimEnd('\')
foreach ($forbidden in 'releases', 'dist', 'install') {
    $f = [IO.Path]::GetFullPath((Join-Path $root $forbidden)).TrimEnd('\')
    if ($OutDir -eq $f -or $OutDir.StartsWith("$f\", [StringComparison]::OrdinalIgnoreCase)) { throw "Refusing to write into $forbidden\" }
}
if ((Test-Path $OutDir) -and (Get-ChildItem -LiteralPath $OutDir -Force | Select-Object -First 1)) {
    if (-not $Force) { throw "$OutDir is not empty (use -Force to replace it)" }
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

$buildspec = Get-Content -Raw (Join-Path $root 'buildspec.json') | ConvertFrom-Json
if (-not $AppPublisher) { $AppPublisher = $buildspec.author }
if (-not $ProjectUrl) { $ProjectUrl = $buildspec.website }
$projectText = if ($ProjectUrl) { $ProjectUrl } else { 'the project''s GitHub page (see the download page you got this file from)' }

# --- 4. Stage (plugin folder without debug symbols) ---------------------------
$stage = Join-Path $OutDir 'stage'
$stagePlugin = Join-Path $stage $name
foreach ($k in $rel.Keys) {
    $dst = Join-Path $stagePlugin ($k.Replace('/', '\'))
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -LiteralPath $rel[$k] -Destination $dst
}
$installTxt = (Get-Content -Raw (Join-Path $root 'packaging\windows\INSTALL-windows.txt')).Replace('{VERSION}', $version).Replace('{PROJECT_URL}', $projectText)
Write-Utf8NoBom (Join-Path $stage 'INSTALL.txt') (ConvertTo-Crlf $installTxt)
Write-Utf8NoBom (Join-Path $stage 'LICENSE.txt') (ConvertTo-Crlf (Get-Content -Raw (Join-Path $root 'LICENSE')))

# --- 5. Manual ZIP (deterministic: sorted entries, fixed timestamps) ----------
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipName = "$name-$version-windows-x64-manual.zip"
$zipPath = Join-Path $OutDir $zipName
# Every entry gets the DLL's build time (UTC, whole minutes), so the ZIP does not depend on when it was made.
$t = (Get-Item -LiteralPath $rel["$name.dll"]).LastWriteTimeUtc
$stamp = New-Object DateTimeOffset($t.Year, $t.Month, $t.Day, $t.Hour, $t.Minute, 0, [TimeSpan]::Zero)
$entries = @()
$entries += [pscustomobject]@{ Name = "$name/"; Source = $null }
$dirs = @($rel.Keys | ForEach-Object { $d = Split-Path $_ -Parent; while ($d) { $d.Replace('\', '/'); $d = Split-Path $d -Parent } } | Sort-Object -Unique)
foreach ($d in $dirs) { $entries += [pscustomobject]@{ Name = "$name/$d/"; Source = $null } }
foreach ($k in $rel.Keys) { $entries += [pscustomobject]@{ Name = "$name/$k"; Source = (Join-Path $stagePlugin ($k.Replace('/', '\'))) } }
$entries += [pscustomobject]@{ Name = 'INSTALL.txt'; Source = (Join-Path $stage 'INSTALL.txt') }
$entries += [pscustomobject]@{ Name = 'LICENSE.txt'; Source = (Join-Path $stage 'LICENSE.txt') }
$fs = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
try {
    $zip = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($e in ($entries | Sort-Object Name)) {
            $entry = $zip.CreateEntry($e.Name, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $stamp
            if ($e.Source) {
                $bytes = [IO.File]::ReadAllBytes($e.Source)
                $s = $entry.Open(); try { $s.Write($bytes, 0, $bytes.Length) } finally { $s.Dispose() }
            }
        }
    } finally { $zip.Dispose() }
} finally { $fs.Dispose() }
Write-Host "== $zipName"

# --- 6. Installer ---------------------------------------------------------------
$exeName = "$name-$version-windows-x64-installer.exe"
$isccVersion = ''
if (-not $SkipInstaller) {
    if (-not $Iscc) {
        $candidates = @($env:ISCC, (Join-Path $root '.tools\InnoSetup\ISCC.exe'),
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
            "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe", "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
            "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe") | Where-Object { $_ -and (Test-Path $_) }
        $Iscc = @($candidates)[0]
    }
    if (-not $Iscc -or -not (Test-Path $Iscc)) { throw 'Inno Setup (ISCC.exe) not found. Install Inno Setup 6.7+, pass -Iscc, or use -SkipInstaller.' }
    # ISCC.exe carries no version resource; installer.iss itself requires Inno Setup 6.3+ and prints the version.
    $isccArgs = @("/DAppVersion=$version", "/DPayloadDir=$stagePlugin", "/DOutputDir=$OutDir",
        "/DLicenseFile=$(Join-Path $stage 'LICENSE.txt')", "/DAppPublisher=$AppPublisher")
    if ($ProjectUrl) { $isccArgs += "/DAppURL=$ProjectUrl" }
    $isccArgs += (Join-Path $root 'packaging\windows\installer.iss')
    # Windows PowerShell 5.1 turns a native program's stderr into errors; collect it with 'Continue'.
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $isccLog = & $Iscc @isccArgs 2>&1 | ForEach-Object { "$_" }
    $isccCode = $LASTEXITCODE
    $ErrorActionPreference = $prevPref
    $isccLog | Set-Content -Encoding utf8 (Join-Path $OutDir 'iscc.log')
    if ($isccCode -ne 0) { $isccLog | Select-Object -Last 25 | Write-Host; throw "ISCC failed ($isccCode), see iscc.log" }
    $isccVersion = (($isccLog | Select-String -Pattern 'Inno Setup (\d+\.\d+\.\d+)' | Select-Object -First 1).Matches.Groups[1].Value)
    if (-not (Test-Path (Join-Path $OutDir $exeName))) { throw "ISCC did not produce $exeName" }
    Write-Host "== $exeName (Inno Setup $isccVersion)"
}

# --- 7. Verify after packaging ----------------------------------------------------
$z = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $seen = @{}
    foreach ($entry in $z.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $ms = New-Object IO.MemoryStream
        $s = $entry.Open(); try { $s.CopyTo($ms) } finally { $s.Dispose() }
        $seen[$entry.FullName] = Get-BytesSha256 $ms.ToArray()
    }
} finally { $z.Dispose() }
foreach ($k in $payloadHashes.Keys) {
    if ($seen["$name/$k"] -ne $payloadHashes[$k]) { throw "ZIP verification failed for $name/$k" }
}
$expectedNames = @($payloadHashes.Keys | ForEach-Object { "$name/$_" }) + 'INSTALL.txt', 'LICENSE.txt'
$extraNames = @($seen.Keys | Where-Object { $expectedNames -notcontains $_ })
if ($extraNames -or $seen.Count -ne $expectedNames.Count) { throw "ZIP has unexpected entries: $($extraNames -join ', ')" }
Write-Host "== ZIP verified: $($payloadHashes.Count) plugin files byte-identical to the payload, no debug symbols"

# --- 8. Checksums (written, then re-read and re-verified) -----------------------
$assets = @($zipName) + $(if (-not $SkipInstaller) { @($exeName) } else { @() })
$sumLines = $assets | Sort-Object | ForEach-Object { "{0}  {1}" -f (Get-Sha256 (Join-Path $OutDir $_)), $_ }
$sumsPath = Join-Path $OutDir 'SHA256SUMS-windows.txt'
Write-Utf8NoBom $sumsPath (($sumLines -join "`n") + "`n")
foreach ($line in (Get-Content $sumsPath)) {
    $h, $n = $line -split '  ', 2
    if ((Get-Sha256 (Join-Path $OutDir $n)) -ne $h) { throw "Checksum re-verification failed for $n" }
}

$assetHashes = [ordered]@{}
foreach ($a in $assets) { $assetHashes[$a] = Get-Sha256 (Join-Path $OutDir $a) }
$info = [ordered]@{
    version = $version; pinned = $pinned; payload_files = $payloadHashes; assets = $assetHashes
    inno_setup = $isccVersion; app_publisher = $AppPublisher; project_url = $ProjectUrl; created = (Get-Date).ToString('o')
}
Write-Utf8NoBom (Join-Path $OutDir 'package-info.json') ($info | ConvertTo-Json -Depth 5)
Remove-Item -LiteralPath $stage -Recurse -Force
Get-Content $sumsPath
Write-Host "== done: $OutDir"
