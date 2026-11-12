<#
.SYNOPSIS
  Installs, replaces or removes ONLY the Multi-Stream Statistics plugin files
  in an OBS 33 portable installation, with a hash-verified record of every file.
  Never run against the real OBS without the user's approval.

  Install   : refuses if OBS is running or the destination folder already
              exists; copies the 4 package files of -Source into
              <ObsRoot>\plugins\obs-multistream-stats\; verifies SHA-256 of
              every copy; writes the install manifest.
  Replace   : upgrade or rollback (e.g. 1.0.0 -> 1.0.1, or back). Verifies that
              every installed file still matches the manifest (refuses if
              anything was modified), removes exactly those files, installs
              -Source, keeps the previous manifest as
              install-manifest-<oldversion>.json next to the new one.
  Uninstall : removes exactly the files listed in the manifest (only if their
              hash still matches), then the folders the install created.

  Never touches any other file. The plugin's own runtime state
  (config\obs-studio\plugin_config\obs-multistream-stats\) is kept unless
  -RemoveRuntimeState is given with Uninstall.

.EXAMPLE
  tools\install-plugin.ps1 -Mode Replace -Source releases\1.0.1\obs-multistream-stats   # upgrade
  tools\install-plugin.ps1 -Mode Replace -Source releases\1.0.0\obs-multistream-stats   # rollback
#>
param(
    [ValidateSet('Install', 'Replace', 'Uninstall')]
    [string]$Mode = 'Install',
    [string]$ObsRoot = "$env:USERPROFILE\Documents\OBS Studio",
    [string]$Source = '',
    [string]$ManifestPath = '',
    [switch]$RemoveRuntimeState,
    # TEST ONLY (sandbox): fail right after the old files were removed, to
    # prove that Replace restores the previous version.
    [switch]$TestFailAfterRemove
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = if ($Source) { (Resolve-Path $Source).Path.TrimEnd('\') } else { Join-Path $root 'dist\obs-multistream-stats' }
$dest = Join-Path $ObsRoot 'plugins\obs-multistream-stats'
$manifestPath = if ($ManifestPath) { $ManifestPath } else { Join-Path $root 'install\install-manifest.json' }

if (Get-Process -Name obs64, obs32 -ErrorAction SilentlyContinue) {
    throw 'OBS is running. Close OBS first (this script never terminates it).'
}

function Assert-Package([string]$dir) {
    $files = Get-ChildItem -LiteralPath $dir -Recurse -File
    $expected = @('obs-multistream-stats.dll', 'obs-multistream-stats.pdb', 'data\manifest.json', 'data\locale\en-US.ini')
    $rels = $files | ForEach-Object { $_.FullName.Substring($dir.Length + 1) }
    if (Compare-Object ($rels | Sort-Object) ($expected | Sort-Object)) { throw "Unexpected package contents in ${dir}: $($rels -join ', ')" }
    return $files
}

function Install-Files {
    if (-not (Test-Path (Join-Path $ObsRoot 'bin\64bit\obs64.exe'))) { throw "No OBS at $ObsRoot" }
    if (-not (Test-Path (Join-Path $ObsRoot 'plugins'))) { throw "$ObsRoot\plugins does not exist (not an OBS 33 layout?)" }
    if (Test-Path $dest) { throw "$dest already exists - refusing to overwrite. Use -Mode Replace or Uninstall first." }
    $files = Assert-Package $src
    $record = @()
    $createdDirs = New-Object System.Collections.Generic.List[string]
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($src.Length + 1)
        $target = Join-Path $dest $rel
        $d = Split-Path $target
        $chain = @()
        while (-not (Test-Path $d)) { $chain += $d; $d = Split-Path $d }
        [array]::Reverse($chain)
        foreach ($c in $chain) { New-Item -ItemType Directory $c | Out-Null; $createdDirs.Add($c) }
        Copy-Item -LiteralPath $f.FullName -Destination $target
        $hs = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
        $hd = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($hs -ne $hd) { throw "Hash mismatch after copying $rel" }
        $record += [ordered]@{ path = $target; size = (Get-Item -LiteralPath $target).Length; sha256 = $hd }
    }
    New-Item -ItemType Directory -Force (Split-Path $manifestPath) | Out-Null
    $version = (Get-Content -Raw (Join-Path $src 'data\manifest.json') | ConvertFrom-Json).version
    [ordered]@{
        installed           = (Get-Date).ToString('o')
        plugin              = 'obs-multistream-stats'
        version             = $version
        source              = $src
        obs_version         = (Get-Item (Join-Path $ObsRoot 'bin\64bit\obs64.exe')).VersionInfo.ProductVersion
        destination         = $dest
        created_directories = $createdDirs
        files               = $record
    } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $manifestPath
    "Installed version $version ($($record.Count) files) into $dest (all hashes verified)"
    $record | ForEach-Object { "  {0}  {1,9}  {2}" -f $_.sha256, $_.size, $_.path }
    "Record: $manifestPath"
}

function Get-Manifest {
    if (-not (Test-Path $manifestPath)) { throw "No install manifest at $manifestPath - nothing recorded." }
    return (Get-Content -Raw $manifestPath | ConvertFrom-Json)
}

function Assert-Unmodified($m) {
    foreach ($f in $m.files) {
        if (-not (Test-Path -LiteralPath $f.path)) { throw "Installed file missing: $($f.path) - refusing (inspect manually)." }
        if ((Get-FileHash -LiteralPath $f.path -Algorithm SHA256).Hash -ne $f.sha256) { throw "Refusing: $($f.path) differs from what was installed." }
    }
    $extra = @(Get-ChildItem -LiteralPath $m.destination -Recurse -File | Where-Object { $m.files.path -notcontains $_.FullName })
    if ($extra.Count) { throw "Refusing: unrecorded files in $($m.destination): $($extra.FullName -join ', ')" }
}

function Remove-Files($m) {
    # All-or-nothing: verify every recorded file BEFORE deleting any of them.
    foreach ($f in $m.files) {
        if ((Test-Path -LiteralPath $f.path) -and (Get-FileHash -LiteralPath $f.path -Algorithm SHA256).Hash -ne $f.sha256) {
            throw "Refusing to delete anything: $($f.path) differs from what was installed"
        }
    }
    foreach ($f in $m.files) {
        if (-not (Test-Path -LiteralPath $f.path)) { "  already absent: $($f.path)"; continue }
        Remove-Item -LiteralPath $f.path -Confirm:$false
        "  removed $($f.path)"
    }
    $dirs = @($m.created_directories)
    [array]::Reverse($dirs)
    foreach ($d in $dirs) {
        if ((Test-Path $d) -and -not (Get-ChildItem -Force -LiteralPath $d)) { Remove-Item -LiteralPath $d -Confirm:$false; "  removed empty dir $d" }
        elseif (Test-Path $d) { "  kept non-empty dir $d" }
    }
}

switch ($Mode) {
    'Install' { Install-Files }
    'Replace' {
        $old = Get-Manifest
        if ($old.destination -ne $dest) { throw "Manifest is for $($old.destination), not $dest" }
        [void](Assert-Package $src)
        $newVersion = (Get-Content -Raw (Join-Path $src 'data\manifest.json') | ConvertFrom-Json).version
        Assert-Unmodified $old
        "Replacing installed $($old.version) with $newVersion"
        $recordDir = Split-Path $manifestPath
        $archived = Join-Path $recordDir ("install-manifest-{0}.json" -f $old.version)
        Copy-Item -LiteralPath $manifestPath -Destination $archived -Force

        # 1. Hash-verified copy of the currently installed files (workspace side),
        #    so an interrupted swap can always be undone.
        $backup = Join-Path $recordDir ("replaced-{0}-{1}\obs-multistream-stats" -f $old.version, (Get-Date -Format 'yyyyMMdd-HHmmss'))
        foreach ($f in $old.files) {
            $rel = $f.path.Substring($old.destination.Length + 1)
            $b = Join-Path $backup $rel
            New-Item -ItemType Directory -Force (Split-Path $b) | Out-Null
            Copy-Item -LiteralPath $f.path -Destination $b
            if ((Get-FileHash -LiteralPath $b).Hash -ne $f.sha256) { throw "Backup of $rel failed verification - nothing was changed." }
        }
        "Backup of installed $($old.version): $backup"

        # 2. Swap. Nothing is written to the pipeline inside the critical
        #    section (a stopped pipeline aborts a script at its next write).
        #    If the swap does not complete (error, Ctrl+C, stopped pipeline),
        #    the finally block silently restores the previous version.
        $done = $false
        $messages = @()
        try {
            $messages += Remove-Files $old
            if ($TestFailAfterRemove) { throw 'TEST: simulated failure after removing the old files' }
            $messages += Install-Files
            $done = $true
        } finally {
            if (-not $done) {
                if (Test-Path $dest) { Remove-Item -LiteralPath $dest -Recurse -Force -Confirm:$false }
                $script:src = $backup
                $null = Install-Files
                Copy-Item -LiteralPath $archived -Destination $manifestPath -Force
                "$(Get-Date -Format o) Replace $($old.version) -> $newVersion interrupted; restored $($old.version) from $backup" |
                    Add-Content -LiteralPath (Join-Path $recordDir 'install-log.txt')
                try { $host.UI.WriteWarningLine("Replace did not complete - $($old.version) was restored from $backup") } catch {}
            }
        }
        "$(Get-Date -Format o) Replace $($old.version) -> $newVersion completed" | Add-Content -LiteralPath (Join-Path $recordDir 'install-log.txt')
        $messages
        "Previous record kept: $archived"
    }
    'Uninstall' {
        $m = Get-Manifest
        $messages = Remove-Files $m
        $messages
        if ($RemoveRuntimeState) {
            $state = Join-Path $ObsRoot 'config\obs-studio\plugin_config\obs-multistream-stats'
            if (Test-Path $state) { Remove-Item -LiteralPath $state -Recurse -Confirm:$false; "  removed runtime state $state" }
        }
        'Uninstall complete.'
    }
}
